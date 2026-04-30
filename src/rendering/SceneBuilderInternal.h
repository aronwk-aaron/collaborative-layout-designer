#pragma once

// Shared internals for the SceneBuilder translation units. Kept out of the
// public SceneBuilder.h so downstream code (and Qt moc) don't pull in the
// LayerSink / data-key plumbing.

#include "SceneBuilder.h"

#include <QGraphicsItem>
#include <QGraphicsScene>
#include <QList>

namespace bld::rendering::detail {

constexpr int kPx = SceneBuilder::kPixelsPerStud;

inline double studToPx(double s) { return s * kPx; }

// Metadata keys attached to each QGraphicsItem via setData(). The edit
// pipeline looks up the mutated brick / label / venue item by reading
// these on mouse release or key press.
constexpr int kBrickDataLayerIndex = 0;
constexpr int kBrickDataGuid       = 1;
constexpr int kBrickDataKind       = 2;  // "brick" | "label" | "venue" | "connDot" | "moduleAnnotation"

// Sink passed to each addXxxLayer() helper. Items go directly into the
// scene (no QGraphicsItemGroup parent) so hit-testing / selection /
// event dispatch isn't intercepted by a container. Tracks every item
// spawned so visibility toggles and clear() can iterate them; biases
// zValue by baseZ so higher layers outrank lower layers.
struct LayerSink {
    QGraphicsScene& scene;
    QList<QGraphicsItem*>& items;
    double baseZ = 0.0;
    bool   visible = true;

    void add(QGraphicsItem* it) {
        if (!it) return;
        if (it->zValue() == 0.0) it->setZValue(baseZ);
        else                     it->setZValue(baseZ + it->zValue());
        it->setVisible(visible);
        scene.addItem(it);
        items.append(it);
    }
};

}  // namespace bld::rendering::detail
