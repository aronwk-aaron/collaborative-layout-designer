#pragma once

#include "../rendering/SceneBuilder.h"

#include <QGraphicsItem>
#include <QString>

// Helpers shared between MapView.cpp and its companion translation units
// (MapViewDrag.cpp, MapViewClipboard.cpp). Lives in the detail namespace so
// nothing outside the MapView implementation pulls it in.

namespace cld::ui::detail {

// Keep these in sync with SceneBuilder.cpp (anonymous namespace).
constexpr int kBrickDataLayerIndex = 0;
constexpr int kBrickDataGuid       = 1;
constexpr int kBrickDataKind       = 2;

inline bool isBrickItem(const QGraphicsItem* it) {
    return it && it->data(kBrickDataKind).toString() == QStringLiteral("brick");
}
inline bool isTextItem(const QGraphicsItem* it) {
    return it && it->data(kBrickDataKind).toString() == QStringLiteral("text");
}
inline bool isRulerItem(const QGraphicsItem* it) {
    return it && it->data(kBrickDataKind).toString() == QStringLiteral("ruler");
}
inline bool isLabelItem(const QGraphicsItem* it) {
    return it && it->data(kBrickDataKind).toString() == QStringLiteral("label");
}
inline bool isVenueItem(const QGraphicsItem* it) {
    return it && it->data(kBrickDataKind).toString() == QStringLiteral("venue");
}

inline double studToPx() { return rendering::SceneBuilder::kPixelsPerStud; }

}  // namespace cld::ui::detail
