#include "SetIO.h"

#include <QFileInfo>
#include <QSaveFile>
#include <QXmlStreamWriter>

namespace bld::saveload {

bool writeSetXml(const QString& filePath,
                 const SetManifest& manifest,
                 QString* error) {
    QSaveFile f(filePath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (error) *error = QStringLiteral("Cannot open %1 for write").arg(filePath);
        return false;
    }
    QXmlStreamWriter w(&f);
    w.setAutoFormatting(true);
    w.setAutoFormattingIndent(1);
    // BrickTracks set files use tab indentation — match for a tidy diff
    // against any hand-authored sets in the same library.
    w.setAutoFormattingIndent(-1);
    w.writeStartDocument(QStringLiteral("1.0"));
    w.writeStartElement(QStringLiteral("group"));

    if (!manifest.author.isEmpty())
        w.writeTextElement(QStringLiteral("Author"), manifest.author);

    if (!manifest.name.isEmpty()) {
        w.writeStartElement(QStringLiteral("Description"));
        w.writeTextElement(QStringLiteral("en"), manifest.name);
        w.writeEndElement();  // Description
    }

    if (!manifest.sortingKey.isEmpty())
        w.writeTextElement(QStringLiteral("SortingKey"), manifest.sortingKey);

    w.writeTextElement(QStringLiteral("CanUngroup"),
                       manifest.canUngroup ? QStringLiteral("true")
                                           : QStringLiteral("false"));

    w.writeStartElement(QStringLiteral("SubPartList"));
    for (const SetSubpart& sp : manifest.subparts) {
        w.writeStartElement(QStringLiteral("SubPart"));
        w.writeAttribute(QStringLiteral("id"), sp.partKey);
        w.writeStartElement(QStringLiteral("position"));
        w.writeTextElement(QStringLiteral("x"),
                           QString::number(sp.positionStuds.x(), 'f', 6));
        w.writeTextElement(QStringLiteral("y"),
                           QString::number(sp.positionStuds.y(), 'f', 6));
        w.writeEndElement();  // position
        w.writeTextElement(QStringLiteral("angle"),
                           QString::number(sp.angleDegrees, 'f', 4));
        w.writeEndElement();  // SubPart
    }
    w.writeEndElement();  // SubPartList

    w.writeEndElement();  // group
    w.writeEndDocument();

    if (!f.commit()) {
        if (error) *error = QStringLiteral("Could not commit %1").arg(filePath);
        return false;
    }
    return true;
}

}  // namespace bld::saveload
