#pragma once

#include <QPointF>
#include <QRectF>
#include <QString>
#include <QVector>

namespace bld::core { class Map; }

namespace bld::edit {

enum class VenueViolationKind {
    OutsideVenue,          // brick's footprint extends past the venue outline
    InsideWalkwayBuffer,   // brick encroaches on the minWalkwayStuds band inside a Door/Open edge
    IntersectsObstacle,    // brick overlaps a VenueObstacle polygon
};

struct VenueViolation {
    VenueViolationKind kind;
    QString brickGuid;
    int     layerIndex = -1;
    QRectF  brickBoundsStuds;     // axis-aligned for UX click-to-jump
    QString description;          // human-readable explanation
};

// Walk every brick layer and flag every brick that violates the project's
// venue layout rules. Cheap to call (O(bricks × edges + bricks × obstacles));
// safe to call from the idle-timer / on-change handler. If the project has
// no enabled venue, returns an empty list.
QVector<VenueViolation> validateVenue(const core::Map& map);

}  // namespace bld::edit
