#include "Connectivity.h"

#include "../core/Brick.h"
#include "../core/Layer.h"
#include "../core/LayerBrick.h"
#include "../core/Map.h"
#include "../parts/PartsLibrary.h"

#include <QHash>
#include <QPointF>

#include <cmath>
#include <vector>

namespace cld::edit {

using core::Brick;
using core::LayerBrick;
using core::LayerKind;
using core::Map;

namespace {

QPointF rotatePoint(QPointF p, double degrees) {
    const double r = degrees * M_PI / 180.0;
    const double c = std::cos(r), s = std::sin(r);
    return { p.x() * c - p.y() * s, p.x() * s + p.y() * c };
}

struct WorldConn {
    Brick*   brick = nullptr;
    int      connIdx = -1;
    QPointF  worldPos;
    QString  type;
};

// Round a stud coord to a fixed grid cell for spatial bucketing so we
// find coincident connections in O(N). Bucket size 2 studs keeps the
// bucket lookup's ±1 neighbourhood wider than our 1.5-stud matching
// tolerance, so a coincident pair always lands in the same or an
// adjacent bucket.
constexpr double kBucketSize = 2.0;
QPair<int, int> bucketOf(QPointF p) {
    return { static_cast<int>(std::floor(p.x() / kBucketSize)),
             static_cast<int>(std::floor(p.y() / kBucketSize)) };
}

}  // namespace

void rebuildConnectivity(core::Map& map, parts::PartsLibrary& lib) {
    // Collect every brick's connection in world coords. Clear any
    // linkedToId as we go — we're recomputing from scratch based on
    // current world positions.
    std::vector<WorldConn> all;
    all.reserve(256);

    for (auto& layerPtr : map.layers()) {
        if (!layerPtr || layerPtr->kind() != LayerKind::Brick) continue;
        auto& BL = static_cast<LayerBrick&>(*layerPtr);
        for (auto& brick : BL.bricks) {
            auto meta = lib.metadata(brick.partNumber);
            if (!meta) continue;
            const int n = meta->connections.size();
            // Ensure connections vector covers every library connection so
            // indices align between part metadata and stored ConnectionPoint.
            while (static_cast<int>(brick.connections.size()) < n) {
                brick.connections.push_back({});
            }
            for (int i = 0; i < n; ++i) {
                brick.connections[i].linkedToId.clear();  // rebuild
                const auto& c = meta->connections[i];
                if (c.type.isEmpty()) continue;
                WorldConn wc;
                wc.brick = &brick;
                wc.connIdx = i;
                wc.worldPos = brick.displayArea.center()
                              + rotatePoint(c.position, brick.orientation);
                wc.type = c.type;
                all.push_back(wc);
            }
        }
    }

    // Spatial bucketing for O(N) pair finding.
    QHash<QPair<int, int>, std::vector<int>> bucket;
    bucket.reserve(all.size() * 2);
    for (int i = 0; i < static_cast<int>(all.size()); ++i) {
        bucket[bucketOf(all[i].worldPos)].push_back(i);
    }

    // Matching tolerance. Sets (.set.xml) use multi-decimal stud
    // positions like 20.19525 and -3.647675; combined with the
    // subpart's rotation, rounding can push the connection world
    // positions off by up to ~1 stud even when the pieces are
    // visually touching. 1.5 studs gives slack for sets without
    // accepting obviously-separate bricks as linked.
    constexpr double kTolStuds = 1.5;
    constexpr double kTolSq = kTolStuds * kTolStuds;

    for (int i = 0; i < static_cast<int>(all.size()); ++i) {
        const auto& a = all[i];
        if (!a.brick->connections[a.connIdx].linkedToId.isEmpty()) continue;
        const auto ab = bucketOf(a.worldPos);
        for (int dx = -1; dx <= 1; ++dx) {
            for (int dy = -1; dy <= 1; ++dy) {
                auto it = bucket.constFind({ ab.first + dx, ab.second + dy });
                if (it == bucket.constEnd()) continue;
                for (int j : *it) {
                    if (j <= i) continue;
                    const auto& b = all[j];
                    if (a.brick == b.brick) continue;
                    if (a.type != b.type) continue;
                    if (!b.brick->connections[b.connIdx].linkedToId.isEmpty()) continue;
                    const QPointF d = a.worldPos - b.worldPos;
                    if (d.x() * d.x() + d.y() * d.y() > kTolSq) continue;
                    // Match — link both sides to each other's brick guid.
                    a.brick->connections[a.connIdx].linkedToId = b.brick->guid;
                    b.brick->connections[b.connIdx].linkedToId = a.brick->guid;
                    break;
                }
                if (!a.brick->connections[a.connIdx].linkedToId.isEmpty()) break;
            }
            if (!a.brick->connections[a.connIdx].linkedToId.isEmpty()) break;
        }
    }
}

}  // namespace cld::edit
