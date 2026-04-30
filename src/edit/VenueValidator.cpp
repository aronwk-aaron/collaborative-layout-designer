#include "VenueValidator.h"

#include "../core/Layer.h"
#include "../core/LayerBrick.h"
#include "../core/Map.h"
#include "../core/Venue.h"

#include <QPolygonF>

#include <cmath>

namespace bld::edit {

namespace {

// Point-in-closed-polygon via ray casting. Polygon is treated as closed
// (first point connects back to last).
bool pointInPolygon(const QPolygonF& poly, QPointF p) {
    if (poly.size() < 3) return false;
    bool inside = false;
    for (int i = 0, j = poly.size() - 1; i < poly.size(); j = i++) {
        const double yi = poly[i].y(), yj = poly[j].y();
        const double xi = poly[i].x(), xj = poly[j].x();
        const bool intersect =
            ((yi > p.y()) != (yj > p.y())) &&
            (p.x() < (xj - xi) * (p.y() - yi) / (yj - yi + 1e-12) + xi);
        if (intersect) inside = !inside;
    }
    return inside;
}

// Minimum distance from point p to line segment (a, b).
double pointSegmentDistance(QPointF p, QPointF a, QPointF b) {
    const QPointF ab = b - a;
    const QPointF ap = p - a;
    const double lenSq = ab.x() * ab.x() + ab.y() * ab.y();
    if (lenSq < 1e-9) return std::hypot(ap.x(), ap.y());
    double t = (ap.x() * ab.x() + ap.y() * ab.y()) / lenSq;
    t = std::clamp(t, 0.0, 1.0);
    const QPointF foot(a.x() + ab.x() * t, a.y() + ab.y() * t);
    return std::hypot(p.x() - foot.x(), p.y() - foot.y());
}

// Build the venue's outline polygon from its edge polylines. Edges are
// stored as 2+ point polylines; we walk them in order and collect unique
// vertices.
QPolygonF outlinePolygon(const core::Venue& v) {
    QPolygonF poly;
    for (const auto& e : v.edges) {
        for (const QPointF& p : e.polyline) {
            if (poly.isEmpty() || (poly.back() - p).manhattanLength() > 1e-6) {
                poly.append(p);
            }
        }
    }
    // Drop trailing duplicate of the first point if present.
    if (poly.size() > 1 && (poly.front() - poly.back()).manhattanLength() < 1e-6) {
        poly.remove(poly.size() - 1);
    }
    return poly;
}

// Minimum distance from the brick's footprint (4 corners + centre
// sample) to any non-Wall edge segment. Used to test walkway buffer.
double distanceToNonWallEdges(const core::Venue& v, QRectF brick) {
    QVector<QPointF> samples{
        brick.topLeft(), brick.topRight(),
        brick.bottomLeft(), brick.bottomRight(),
        brick.center()
    };
    double best = std::numeric_limits<double>::max();
    for (const auto& e : v.edges) {
        if (e.kind == core::EdgeKind::Wall) continue;
        const auto& poly = e.polyline;
        for (int i = 0; i + 1 < poly.size(); ++i) {
            for (const auto& s : samples) {
                best = std::min(best, pointSegmentDistance(s, poly[i], poly[i + 1]));
            }
        }
    }
    return best;
}

}  // namespace

QVector<VenueViolation> validateVenue(const core::Map& map) {
    QVector<VenueViolation> out;
    if (!map.sidecar.venue || !map.sidecar.venue->enabled) return out;
    const auto& v = *map.sidecar.venue;
    const QPolygonF outline = outlinePolygon(v);
    const bool haveOutline = outline.size() >= 3;
    const double minBuffer = v.minWalkwayStuds;

    // Pre-build obstacle polygons.
    QVector<QPolygonF> obstacleShapes;
    obstacleShapes.reserve(v.obstacles.size());
    for (const auto& ob : v.obstacles) obstacleShapes.append(QPolygonF(ob.polygon));

    for (int li = 0; li < static_cast<int>(map.layers().size()); ++li) {
        auto* layer = map.layers()[li].get();
        if (!layer || layer->kind() != core::LayerKind::Brick) continue;
        const auto& BL = static_cast<const core::LayerBrick&>(*layer);
        for (const auto& b : BL.bricks) {
            const QRectF box = b.displayArea;
            VenueViolation viol;
            viol.brickGuid       = b.guid;
            viol.layerIndex      = li;
            viol.brickBoundsStuds = box;

            // a) Outside venue — any corner of the brick outside the
            // outline polygon.
            if (haveOutline) {
                const QVector<QPointF> corners{
                    box.topLeft(), box.topRight(),
                    box.bottomLeft(), box.bottomRight()
                };
                bool anyOutside = false;
                for (const QPointF& c : corners) {
                    if (!pointInPolygon(outline, c)) { anyOutside = true; break; }
                }
                if (anyOutside) {
                    viol.kind = VenueViolationKind::OutsideVenue;
                    viol.description = QObject::tr("Brick extends past the venue outline");
                    out.append(viol);
                    continue;
                }
            }

            // b) Walkway buffer — distance from brick to any Door/Open
            // edge less than minWalkwayStuds.
            if (haveOutline && minBuffer > 0.01) {
                const double d = distanceToNonWallEdges(v, box);
                if (d < minBuffer) {
                    viol.kind = VenueViolationKind::InsideWalkwayBuffer;
                    viol.description = QObject::tr(
                        "Brick is %1 studs from a door/open edge (buffer %2)")
                        .arg(d, 0, 'f', 1).arg(minBuffer, 0, 'f', 1);
                    out.append(viol);
                    // Don't 'continue' — a brick can ALSO hit an obstacle.
                }
            }

            // c) Obstacle overlap — any corner inside any obstacle
            // polygon.
            for (int oi = 0; oi < obstacleShapes.size(); ++oi) {
                const auto& op = obstacleShapes[oi];
                const QVector<QPointF> corners{
                    box.topLeft(), box.topRight(),
                    box.bottomLeft(), box.bottomRight()
                };
                bool hit = false;
                for (const QPointF& c : corners) {
                    if (pointInPolygon(op, c)) { hit = true; break; }
                }
                if (hit) {
                    viol.kind = VenueViolationKind::IntersectsObstacle;
                    const auto& ob = v.obstacles[oi];
                    viol.description = ob.label.isEmpty()
                        ? QObject::tr("Brick overlaps a venue obstacle")
                        : QObject::tr("Brick overlaps venue obstacle '%1'").arg(ob.label);
                    out.append(viol);
                    break;
                }
            }
        }
    }
    return out;
}

}  // namespace bld::edit
