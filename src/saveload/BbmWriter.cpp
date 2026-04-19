#include "BbmWriter.h"

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
    w.writeAttribute(QStringLiteral("xmlns:xsi"), QStringLiteral("http://www.w3.org/2001/XMLSchema-instance"));
    w.writeAttribute(QStringLiteral("xmlns:xsd"), QStringLiteral("http://www.w3.org/2001/XMLSchema"));

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
    // Phase 2+: iterate m.layers() and emit each via a LayerWriter dispatch.
    w.writeEndElement();
}

}

WriteResult writeBbm(const core::Map& m, QIODevice& output) {
    QXmlStreamWriter w(&output);
    w.setAutoFormatting(false); // vanilla XmlSerializer output is non-indented
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
