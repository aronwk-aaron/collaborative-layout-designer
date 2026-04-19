#pragma once

#include <QDateTime>
#include <QSet>
#include <QString>
#include <QTransform>

namespace cld::core {

// Fork-only cross-layer group: a named bundle of items that span multiple
// layers and can be transformed as a unit. Vanilla BlueBrick's per-layer
// Group structure can express part of this, but the cross-layer linkage
// lives in the .bbm.cld sidecar.
//
// `members` holds item guids across any layers — bricks, rulers, texts,
// areas (areas have no guid but can be referenced by (layer, x, y) in a
// future extension).
struct Module {
    QString id;
    QString name;
    QSet<QString> memberIds;
    QTransform transform;                       // local-to-world of the module as a unit
    QString    sourceFile;                      // non-empty if imported from a .bbm
    QDateTime  importedAt;                      // when imported (if sourceFile non-empty)
};

}
