// Electric-circuits render overlay. Toggled via View > Electric Circuits
// (QSettings key "view/electricCircuits"). Matches BlueBrick's visual model:
//
//  - Each part with electricPlug pairs (+1/-1) defines one or more in-part
//    "circuits". We draw those as two parallel lines between the pair's
//    connection points: one OrangeRed (rail +1 side) and one Cyan (rail -1
//    side), each offset 0.5 studs perpendicularly.
//
//  - Polarity is propagated across connected bricks via BFS so the red/blue
//    assignment stays consistent across an entire wired run of track.
//
//  - Short circuits (same polarity appears on both ends of a circuit after BFS)
//    are flagged with an orange diamond at the offending connection point.
//
//  - Line width scales with kPixelsPerStud (0.5 studs wide, same as BlueBrick).

#include "SceneBuilder.h"
#include "SceneBuilderInternal.h"

#include "../core/LayerBrick.h"
#include "../core/Map.h"
#include "../parts/PartsLibrary.h"

#include <QBrush>
#include <QGraphicsEllipseItem>
#include <QGraphicsLineItem>
#include <QGraphicsPolygonItem>
#include <QGraphicsScene>
#include <QPen>
#include <QSettings>

#include <cmath>
#include <unordered_map>

namespace cld::rendering {

using detail::LayerSink;

namespace {

// World-space position (pixels) of a connection point on a placed brick.
QPointF connWorldPx(const core::Brick& b,
                    const parts::PartConnectionPoint& c,
                    double px) {
    const double r = b.orientation * M_PI / 180.0;
    const double cs = std::cos(r), sn = std::sin(r);
    const QPointF centre = b.displayArea.center();
    return QPointF(
        (centre.x() + c.position.x() * cs - c.position.y() * sn) * px,
        (centre.y() + c.position.x() * sn + c.position.y() * cs) * px);
}

// Per-connection polarity state used during BFS.
// +timestamp = rail +1, -timestamp = rail -1, 0 = unvisited.
struct ConnState {
    short polarity = 0;
    bool  hasShortcut = false;
};

}  // namespace

void SceneBuilder::addElectricCircuits(const core::Map& map) {
    QSettings s;
    if (!s.value(QStringLiteral("view/electricCircuits"), false).toBool()) return;

    // -------------------------------------------------------------------
    // 1. Collect every (brick, circuit) pair that has at least one
    //    in-part circuit. Build a lookup: brickGuid -> brick pointer and
    //    per-connection state.
    // -------------------------------------------------------------------
    struct BrickEntry {
        const core::Brick*          brick   = nullptr;
        const parts::PartMetadata*  meta    = nullptr;
        // Polarity per connection index. Sized to meta->connections.size().
        QVector<ConnState>          state;
    };

    QHash<QString, BrickEntry> entries;

    for (const auto& layerPtr : map.layers()) {
        if (!layerPtr || layerPtr->kind() != core::LayerKind::Brick) continue;
        const auto& bl = static_cast<const core::LayerBrick&>(*layerPtr);
        for (const auto& b : bl.bricks) {
            if (b.guid.isEmpty()) continue;
            auto meta = parts_.metadata(b.partNumber);
            if (!meta || meta->electricCircuits.isEmpty()) continue;
            BrickEntry e;
            e.brick = &b;
            e.meta  = new parts::PartMetadata(*meta);   // owned copy for BFS lifetime
            e.state.resize(meta->connections.size());
            entries.insert(b.guid, std::move(e));
        }
    }
    if (entries.isEmpty()) return;

    // -------------------------------------------------------------------
    // 2. BFS to assign consistent polarity across connected bricks,
    //    matching ElectricCircuitChecker.cs logic.
    // -------------------------------------------------------------------
    short stamp = 1;

    auto propagate = [&](const QString& startGuid) {
        BrickEntry* startEntry = entries.find(startGuid) != entries.end()
                                 ? &entries[startGuid] : nullptr;
        if (!startEntry) return;

        ++stamp;
        if (stamp == std::numeric_limits<short>::max()) stamp = 1;

        QList<QString> toExplore;
        toExplore.append(startGuid);

        // Seed the first circuit's first connection with +stamp.
        const int seed1 = startEntry->meta->electricCircuits[0].index1;
        startEntry->state[seed1].polarity = stamp;

        // If it's already linked to another electric brick, seed the partner.
        if (seed1 < static_cast<int>(startEntry->brick->connections.size())) {
            const QString& partnerGuid = startEntry->brick->connections[seed1].linkedToId;
            if (!partnerGuid.isEmpty() && entries.contains(partnerGuid)) {
                entries[partnerGuid].state[seed1].polarity = (short)(-stamp);
                toExplore.append(partnerGuid);
            }
        }

        while (!toExplore.isEmpty()) {
            const QString guid = toExplore.takeFirst();
            BrickEntry* entry = &entries[guid];
            bool needReexplore = false;

            for (const auto& circuit : entry->meta->electricCircuits) {
                ConnState& s1 = entry->state[circuit.index1];
                ConnState& s2 = entry->state[circuit.index2];

                // Ensure s1 carries the incoming electricity; swap if needed.
                ConnState* start = &s1;
                ConnState* end   = &s2;
                int startIdx = circuit.index1;
                int endIdx   = circuit.index2;
                if (std::abs(s2.polarity) == stamp) {
                    std::swap(start, end);
                    std::swap(startIdx, endIdx);
                }

                if (std::abs(start->polarity) != stamp) {
                    needReexplore = true;
                    continue;
                }

                // Short circuit: end already set to same polarity.
                if (end->polarity == start->polarity) {
                    start->hasShortcut = true;
                    continue;
                }

                // Transfer polarity to end if not yet visited.
                if (end->polarity != -(start->polarity)) {
                    end->polarity = (short)(-(start->polarity));

                    if (needReexplore) {
                        toExplore.prepend(guid);
                        needReexplore = false;
                    }

                    // Propagate to linked neighbor brick.
                    if (endIdx < static_cast<int>(entry->brick->connections.size())) {
                        const QString& neighborGuid =
                            entry->brick->connections[endIdx].linkedToId;
                        if (!neighborGuid.isEmpty() && entries.contains(neighborGuid)) {
                            ConnState& nState = entries[neighborGuid].state[endIdx];
                            if (nState.polarity == end->polarity) {
                                end->hasShortcut = true;
                            } else if (nState.polarity != -(end->polarity)) {
                                nState.polarity = (short)(-(end->polarity));
                                toExplore.append(neighborGuid);
                            }
                        }
                    }
                }
            }
        }
    };

    // Run BFS from every electric brick not yet stamped.
    for (auto it = entries.begin(); it != entries.end(); ++it) {
        BrickEntry& e = it.value();
        if (!e.meta->electricCircuits.isEmpty()) {
            const int i0 = e.meta->electricCircuits[0].index1;
            if (std::abs(e.state[i0].polarity) != stamp) {
                propagate(it.key());
            }
        }
    }

    // -------------------------------------------------------------------
    // 3. Render: two parallel lines per circuit (OrangeRed + Cyan),
    //    offset 0.5 studs perpendicularly. Width = 0.5 studs in pixels.
    //    Then shortcut diamonds on top.
    // -------------------------------------------------------------------
    const double px = kPixelsPerStud;
    const double halfOffset = 0.5 * px;    // perpendicular offset in px
    const double lineWidth  = 0.5 * px;    // pen width in px

    static const QColor kRed  = QColor(255, 69, 0);    // OrangeRed
    static const QColor kBlue = QColor(  0, 255, 255); // Cyan
    static const QColor kShortcut = QColor(255, 165, 0); // Orange

    LayerSink sink{ scene_, electricItems_, 5e5, true };

    auto makePen = [&](QColor col) {
        QPen p(col);
        p.setWidthF(lineWidth);
        return p;
    };

    // Draw the two-rail lines for every in-part circuit.
    for (auto it = entries.cbegin(); it != entries.cend(); ++it) {
        const BrickEntry& e = it.value();
        for (const auto& circuit : e.meta->electricCircuits) {
            const QPointF p1 = connWorldPx(*e.brick, e.meta->connections[circuit.index1], px);
            const QPointF p2 = connWorldPx(*e.brick, e.meta->connections[circuit.index2], px);

            const QPointF delta = p2 - p1;
            const double len = std::hypot(delta.x(), delta.y());
            if (len < 0.5) continue;

            // Unit direction and perpendicular normal.
            const QPointF dir(delta.x() / len, delta.y() / len);
            const QPointF norm(-dir.y() * halfOffset, dir.x() * halfOffset);

            // Determine which line gets red vs blue based on polarity.
            // positive polarity on index1 → line1 (norm side) = red.
            const ConnState& cs1 = e.state[circuit.index1];
            const bool posOnSide1 = (cs1.polarity >= 0);  // 0 = unvisited, treat as +
            const QPen pen1 = makePen(posOnSide1 ? kRed : kBlue);
            const QPen pen2 = makePen(posOnSide1 ? kBlue : kRed);

            auto* line1 = new QGraphicsLineItem(
                QLineF(p1 + norm, p2 + norm));
            line1->setPen(pen1);
            line1->setZValue(500);
            sink.add(line1);

            auto* line2 = new QGraphicsLineItem(
                QLineF(p1 - norm, p2 - norm));
            line2->setPen(pen2);
            line2->setZValue(500);
            sink.add(line2);
        }
    }

    // Draw shortcut diamonds over any connection with hasShortcut.
    const double dw = 3.0 * px;   // half-width of diamond in px
    const QPen shortcutPen(kShortcut, lineWidth * 3.0);

    for (auto it = entries.cbegin(); it != entries.cend(); ++it) {
        const BrickEntry& e = it.value();
        for (const auto& circuit : e.meta->electricCircuits) {
            for (int idx : { circuit.index1, circuit.index2 }) {
                if (!e.state[idx].hasShortcut) continue;
                const QPointF c = connWorldPx(*e.brick, e.meta->connections[idx], px);
                QPolygonF diamond;
                diamond << QPointF(c.x() - dw, c.y())
                        << QPointF(c.x(), c.y() - dw)
                        << QPointF(c.x(), c.y() + dw)
                        << QPointF(c.x() + dw, c.y());
                auto* poly = new QGraphicsPolygonItem(diamond);
                poly->setPen(shortcutPen);
                poly->setBrush(Qt::NoBrush);
                poly->setZValue(502);
                sink.add(poly);
            }
        }
    }

    // Clean up owned metadata copies.
    for (auto& e : entries) delete e.meta;
}

}  // namespace cld::rendering
