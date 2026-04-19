#include "BbmWriter.h"

#include "LayerIO.h"
#include "XmlPrimitives.h"

#include "../core/Layer.h"
#include "../core/Map.h"

#include <QBuffer>
#include <QByteArray>
#include <QFile>
#include <QRegularExpression>
#include <QSaveFile>
#include <QXmlStreamWriter>

namespace cld::saveload {

namespace {

void writeDateElement(QXmlStreamWriter& w, const QDate& date) {
    w.writeStartElement(QStringLiteral("Date"));
    xml::writeIntElement(w, QStringLiteral("Day"),   date.day());
    xml::writeIntElement(w, QStringLiteral("Month"), date.month());
    xml::writeIntElement(w, QStringLiteral("Year"),  date.year());
    w.writeEndElement();
}

void writeExportInfo(QXmlStreamWriter& w, const core::ExportInfo& info) {
    w.writeStartElement(QStringLiteral("ExportInfo"));
    xml::writeTextElement(w, QStringLiteral("ExportPath"),     info.exportPath);
    xml::writeIntElement (w, QStringLiteral("ExportFileType"), info.fileTypeIndex);
    xml::writeRectF      (w, QStringLiteral("ExportArea"),     info.area);
    xml::writeDoubleElement(w, QStringLiteral("ExportScale"),  info.scale);
    xml::writeBoolElement(w, QStringLiteral("ExportWatermark"),        info.watermark);
    xml::writeBoolElement(w, QStringLiteral("ExportElectricCircuit"),  info.electricCircuit);
    xml::writeBoolElement(w, QStringLiteral("ExportConnectionPoints"), info.connectionPoints);
    w.writeEndElement();
}

void writeMapBody(QXmlStreamWriter& w, const core::Map& m) {
    // Vanilla BlueBrick's <Map> root has NO xmlns attributes (verified against
    // real saved files; .NET Framework 4.8's XmlSerializer.Serialize over
    // IXmlSerializable does not emit xsi/xsd namespaces by default when the
    // type provides its own WriteXml).
    xml::writeIntElement(w, QStringLiteral("Version"), core::Map::kCurrentDataVersion);
    xml::writeIntElement(w, QStringLiteral("nbItems"), m.nbItems);

    xml::writeColor(w, QStringLiteral("BackgroundColor"), m.backgroundColor);

    xml::writeTextElement(w, QStringLiteral("Author"), m.author);
    xml::writeTextElement(w, QStringLiteral("LUG"),    m.lug);
    xml::writeTextElement(w, QStringLiteral("Event"),  m.event);
    writeDateElement(w, m.date);
    xml::writeTextElement(w, QStringLiteral("Comment"), m.comment);

    writeExportInfo(w, m.exportInfo);

    xml::writeIntElement(w, QStringLiteral("SelectedLayerIndex"), m.selectedLayerIndex);

    w.writeStartElement(QStringLiteral("Layers"));
    for (const auto& layer : m.layers()) {
        if (layer) writeLayer(w, *layer);
    }
    w.writeEndElement();
}

}

namespace {

// Apply three transformations so output matches vanilla .NET Framework 4.8
// XmlSerializer byte-for-byte as closely as possible:
//   1. collapse empty open/close pairs (<Tag></Tag>) to self-closing (<Tag />)
//   2. ensure self-close has a space before /> (<Tag/>  ->  <Tag />)
//   3. translate LF line endings to CRLF
// Known remaining gaps: IsKnownColor round-trip (requires richer color model)
// and preservation of text-content character-entity encoding quirks.
QByteArray vanillaPostProcess(QByteArray xml) {
    // 1) Empty open-close -> self-close. Must run before rule 2.
    //    Does NOT collapse <Tag>content</Tag> — `[^<]*` would match text; we
    //    instead restrict to immediate </Tag> with no body.
    static const QRegularExpression emptyPair(
        QStringLiteral("<([A-Za-z_][A-Za-z0-9_]*)((?:\\s+[A-Za-z_][A-Za-z0-9_]*=\"[^\"]*\")*)></\\1>"));
    xml = QByteArray::fromStdString(
        QString::fromUtf8(xml).replace(emptyPair, QStringLiteral("<\\1\\2 />")).toStdString());

    // 2) Self-close without space -> space before />.
    static const QRegularExpression selfCloseNoSpace(
        QStringLiteral("<([A-Za-z_][A-Za-z0-9_]*((?:\\s+[A-Za-z_][A-Za-z0-9_]*=\"[^\"]*\")*))/>"));
    xml = QByteArray::fromStdString(
        QString::fromUtf8(xml).replace(selfCloseNoSpace, QStringLiteral("<\\1 />")).toStdString());

    // 3) encoding="UTF-8" -> encoding="utf-8" (vanilla emits lowercase).
    xml.replace("encoding=\"UTF-8\"", "encoding=\"utf-8\"");

    // 4) LF -> CRLF. Don't re-translate existing CRLF.
    xml.replace("\r\n", "\n");
    xml.replace("\n", "\r\n");

    // 5) Strip trailing newline(s): vanilla emits </Map> at column 0 with no
    // trailing CR/LF. QXmlStreamWriter::writeEndDocument() appends one.
    while (xml.endsWith('\n') || xml.endsWith('\r')) xml.chop(1);

    return xml;
}

}

WriteResult writeBbm(const core::Map& m, QIODevice& output) {
    QByteArray buf;
    {
        QBuffer memOut(&buf);
        memOut.open(QIODevice::WriteOnly);
        QXmlStreamWriter w(&memOut);
        // .NET Framework 4.8 XmlSerializer emits indented XML with 2-space indent.
        w.setAutoFormatting(true);
        w.setAutoFormattingIndent(2);
        w.writeStartDocument(QStringLiteral("1.0"));
        w.writeStartElement(QStringLiteral("Map"));
        writeMapBody(w, m);
        w.writeEndElement();
        w.writeEndDocument();
        if (w.hasError()) return { false, QStringLiteral("XML writer error") };
    }

    const QByteArray vanillaBytes = vanillaPostProcess(std::move(buf));
    if (output.write(vanillaBytes) != vanillaBytes.size()) {
        return { false, QStringLiteral("Write truncated") };
    }
    return { true, {} };
}

WriteResult writeBbm(const core::Map& m, const QString& path) {
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return { false, QStringLiteral("Cannot open file: %1").arg(file.errorString()) };
    }
    auto r = writeBbm(m, file);
    if (!r.ok) return r;
    if (!file.commit()) return { false, QStringLiteral("Commit failed: %1").arg(file.errorString()) };
    return { true, {} };
}

}
