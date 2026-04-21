#pragma once

#include <QImage>
#include <QString>
#include <QVector>

namespace cld::parts { class PartsLibrary; }

namespace cld::import {

// Write a single BlueBrick part (GIF + XML pair) that represents an
// imported LDraw / Studio / LDD model as a flat top-down sprite.
// The sprite is rendered from whichever parts of the model resolve
// against `lib` (unknown LDraw references contribute nothing). Saves
// into `destLibraryDir` as `<partName>.gif` and `<partName>.xml`.
//
// Returns the new part key (the filename stem without the extension),
// or an empty string on failure; the error is written to *error
// if non-null.
// One free-external connection on the composite, in sprite-local
// coords (origin = sprite centre, studs, y-down). Fed to
// writeImportedModelAsLibraryPart's new overload so the resulting
// library part is snap-compatible with other tracks/rails/etc.
struct ImportedConnection {
    QString type;              // BlueBrick connection type string ("rail", "road", etc.)
    double  xStuds   = 0.0;    // sprite-local x
    double  yStuds   = 0.0;    // sprite-local y
    double  angleDeg = 0.0;    // world-facing angle
};

QString writeImportedModelAsLibraryPart(
    const QString& sourceFilePath,
    QImage         renderedSprite,
    int            widthStuds,
    int            heightStuds,
    const QString& destLibraryDir,
    const QString& authorName,
    QString*       error = nullptr);

// Variant that also writes a <ConnexionList> into the generated XML so
// the composite part is snap-compatible when placed. `connections`
// are in sprite-local coords (the same frame the XML's ConnexionList
// assumes). Empty list ⇒ equivalent to the no-conn overload above.
QString writeImportedModelAsLibraryPart(
    const QString& sourceFilePath,
    QImage         renderedSprite,
    int            widthStuds,
    int            heightStuds,
    const QString& destLibraryDir,
    const QString& authorName,
    const QVector<ImportedConnection>& connections,
    QString*       error = nullptr);

}  // namespace cld::import
