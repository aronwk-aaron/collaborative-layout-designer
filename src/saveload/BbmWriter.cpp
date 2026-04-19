#include "BbmWriter.h"

#include "LayerIO.h"
#include "XmlPrimitives.h"

#include "../core/Layer.h"
#include "../core/Map.h"

#include <QFile>
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

int sumNbItems(const core::Map& m) {
    // Phase 2+: sum each layer's item count.
    (void)m;
    return 0;
}

void writeMapBody(QXmlStreamWriter& w, const core::Map& m) {
    // Vanilla BlueBrick's <Map> root has NO xmlns attributes (verified against
    // real saved files; .NET Framework 4.8's XmlSerializer.Serialize over
    // IXmlSerializable does not emit xsi/xsd namespaces by default when the
    // type provides its own WriteXml).
    xml::writeIntElement(w, QStringLiteral("Version"), core::Map::kCurrentDataVersion);
    xml::writeIntElement(w, QStringLiteral("nbItems"), sumNbItems(m));

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

WriteResult writeBbm(const core::Map& m, QIODevice& output) {
    QXmlStreamWriter w(&output);
    // Vanilla .NET Framework 4.8 XmlSerializer emits indented XML with 2-space
    // indentation. We match this; byte-exact CRLF handling + empty-element
    // spacing (<Tag /> vs <Tag/>) come later once golden-file CI is wired up.
    w.setAutoFormatting(true);
    w.setAutoFormattingIndent(2);
    w.writeStartDocument(QStringLiteral("1.0"));
    w.writeStartElement(QStringLiteral("Map"));
    writeMapBody(w, m);
    w.writeEndElement(); // </Map>
    w.writeEndDocument();
    if (w.hasError()) return { false, QStringLiteral("XML writer error") };
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
