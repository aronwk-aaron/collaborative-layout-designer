// Electric-circuits render overlay. Toggled via View > Electric Circuits
// (QSettings key "view/electricCircuits"). Computes connected components
// over every brick connection whose parts-library metadata flags an
// electricPlug, then draws coloured segments + node dots so the user can
// see which rails are wired together.
//
// Split out of SceneBuilder.cpp to keep that file focused on the
// general build() pipeline; the circuit pass is self-contained and only
// touches SceneBuilder::electricItems_.

#include "SceneBuilder.h"
#include "SceneBuilderInternal.h"

#include "../core/LayerBrick.h"
#include "../core/Map.h"
#include "../parts/PartsLibrary.h"

#include <QBrush>
#include <QGraphicsEllipseItem>
#include <QGraphicsLineItem>
#include <QGraphicsScene>
#include <QPen>
#include <QSettings>

#include <cmath>
#include <functional>
#include <numeric>

namespace cld::rendering {

using detail::LayerSink;

void SceneBuilder::addElectricCircuits(const core::Map& map) {
    QSettings s;
    if (!s.value(QStringLiteral("view/electricCircuits"), false).toBool()) return;

    // Collect every electric connection (brick + conn index with
    // electricPlug != -1) and its world position in pixels. Use a
    // guid+index string as node id so the union-find groups them.
    struct EConn {
        QString  brickGuid;
        int      connIdx = -1;
        QString  linkedToBrick;   // partner brick guid, empty if free
        int      plug = -1;
        QPointF  worldPx;
    };
    QVector<EConn> conns;
    QHash<QString, int> nodeByGuidPlug;

    for (const auto& layerPtr : map.layers()) {
        if (!layerPtr || layerPtr->kind() != core::LayerKind::Brick) continue;
        const auto& bl = static_cast<const core::LayerBrick&>(*layerPtr);
        for (const auto& b : bl.bricks) {
            auto meta = parts_.metadata(b.partNumber);
            if (!meta) continue;
            const QPointF centre = b.displayArea.center();
            const int n = meta->connections.size();
            for (int i = 0; i < n; ++i) {
                const auto& c = meta->connections[i];
                if (c.electricPlug < 0) continue;
                const double r = b.orientation * M_PI / 180.0;
                const double cs = std::cos(r), sn = std::sin(r);
                const QPointF wStuds(centre.x() + c.position.x() * cs - c.position.y() * sn,
                                     centre.y() + c.position.x() * sn + c.position.y() * cs);
                EConn ec;
                ec.brickGuid = b.guid;
                ec.connIdx   = i;
                ec.plug      = c.electricPlug;
                ec.worldPx   = QPointF(wStuds.x() * kPixelsPerStud,
                                       wStuds.y() * kPixelsPerStud);
                if (i < static_cast<int>(b.connections.size()))
                    ec.linkedToBrick = b.connections[i].linkedToId;
                nodeByGuidPlug.insert(b.guid + QLatin1Char('|') + QString::number(i),
                                      conns.size());
                conns.append(ec);
            }
        }
    }
    if (conns.isEmpty()) return;

    // Union-find over electric connections. Two conns are in the same
    // component if:
    //   a) same brick + same plug index (internal pass-through rail), OR
    //   b) linkedToId across a joint with matching plug index.
    QVector<int> parent(conns.size());
    std::iota(parent.begin(), parent.end(), 0);
    std::function<int(int)> find = [&](int x) {
        while (parent[x] != x) { parent[x] = parent[parent[x]]; x = parent[x]; }
        return x;
    };
    auto unite = [&](int a, int b) {
        const int ra = find(a), rb = find(b);
        if (ra != rb) parent[ra] = rb;
    };

    QHash<QString, QVector<int>> byBrick;
    for (int i = 0; i < conns.size(); ++i) byBrick[conns[i].brickGuid].append(i);
    for (auto it = byBrick.cbegin(); it != byBrick.cend(); ++it) {
        const auto& group = it.value();
        for (int i = 0; i < group.size(); ++i)
            for (int j = i + 1; j < group.size(); ++j)
                if (conns[group[i]].plug == conns[group[j]].plug)
                    unite(group[i], group[j]);
    }
    for (int i = 0; i < conns.size(); ++i) {
        const auto& ec = conns[i];
        if (ec.linkedToBrick.isEmpty()) continue;
        auto partnerIt = byBrick.constFind(ec.linkedToBrick);
        if (partnerIt == byBrick.constEnd()) continue;
        for (int pj : partnerIt.value()) {
            if (conns[pj].linkedToBrick != ec.brickGuid) continue;
            if (conns[pj].plug == ec.plug) unite(i, pj);
        }
    }

    static const QColor kPalette[] = {
        QColor(230,  40,  40),  // red
        QColor( 30, 140, 230),  // blue
        QColor( 20, 180,  90),  // green
        QColor(240, 150,  30),  // orange
        QColor(180,  80, 200),  // purple
        QColor( 40, 180, 180),  // teal
        QColor(220, 200,  40),  // yellow
    };
    constexpr int kPaletteN = sizeof(kPalette) / sizeof(kPalette[0]);
    QHash<int, int> colourByRoot;
    int nextColour = 0;
    auto colourFor = [&](int node) {
        const int r = find(node);
        auto it = colourByRoot.constFind(r);
        if (it != colourByRoot.constEnd()) return kPalette[it.value() % kPaletteN];
        colourByRoot.insert(r, nextColour);
        return kPalette[(nextColour++) % kPaletteN];
    };

    LayerSink sink{ scene_, electricItems_, 5e5, true };

    auto drawLine = [&](QPointF a, QPointF b, QColor col){
        auto* line = new QGraphicsLineItem(QLineF(a, b));
        QPen pen(col); pen.setWidthF(3.5); pen.setCosmetic(true);
        line->setPen(pen);
        line->setZValue(500);
        sink.add(line);
    };
    auto drawNode = [&](QPointF p, QColor col){
        constexpr double kR = 4.0;
        auto* dot = new QGraphicsEllipseItem(p.x() - kR, p.y() - kR, kR * 2, kR * 2);
        dot->setPen(QPen(QColor(20, 20, 20), 1.5));
        dot->setBrush(QBrush(col));
        dot->setZValue(501);
        sink.add(dot);
    };

    for (auto it = byBrick.cbegin(); it != byBrick.cend(); ++it) {
        const auto& group = it.value();
        for (int i = 0; i < group.size(); ++i)
            for (int j = i + 1; j < group.size(); ++j)
                if (conns[group[i]].plug == conns[group[j]].plug) {
                    const QColor c = colourFor(group[i]);
                    drawLine(conns[group[i]].worldPx, conns[group[j]].worldPx, c);
                }
    }
    for (int i = 0; i < conns.size(); ++i) {
        const auto& ec = conns[i];
        if (ec.linkedToBrick.isEmpty()) continue;
        auto partnerIt = byBrick.constFind(ec.linkedToBrick);
        if (partnerIt == byBrick.constEnd()) continue;
        for (int pj : partnerIt.value()) {
            if (pj <= i) continue;
            if (conns[pj].linkedToBrick != ec.brickGuid) continue;
            if (conns[pj].plug != ec.plug) continue;
            const QColor c = colourFor(i);
            drawLine(ec.worldPx, conns[pj].worldPx, c);
        }
    }
    for (int i = 0; i < conns.size(); ++i) {
        drawNode(conns[i].worldPx, colourFor(i));
    }
}

}  // namespace cld::rendering
