#pragma once

#include "ColorSpec.h"

#include <QPointF>
#include <QRectF>
#include <QString>
#include <QVector>

namespace bld::core {

enum class EdgeKind {
    Wall,
    Door,
    Open,
};

struct VenueEdge {
    QVector<QPointF> polyline;  // world coords, in studs (converted from mm on load)
    EdgeKind kind = EdgeKind::Wall;
    double   doorWidthStuds = 0.0;    // only meaningful when kind == Door
    QString  label;
};

struct VenueObstacle {
    QVector<QPointF> polygon;          // closed polygon in world studs
    QString label;
};

// Per-project venue definition. At most one venue per project.
struct Venue {
    QString name;
    QVector<VenueEdge>     edges;
    QVector<VenueObstacle> obstacles;
    double minWalkwayStuds = 112.5;   // ~900 mm @ 8 studs/mm
    QRectF layoutBoundsStuds;          // optional reserved layout footprint
    bool   enabled = true;
};

}
