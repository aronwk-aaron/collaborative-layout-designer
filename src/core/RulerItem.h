#pragma once

#include "LayerItem.h"

#include <QColor>
#include <QPointF>
#include <QString>

namespace cld::core {

enum class RulerKind {
    Linear,
    Circular,
};

// Fields common to every RulerItem subtype (matches upstream RulerItem base class).
struct RulerItemBase : LayerItem {
    QColor color = Qt::black;
    float  lineThickness = 1.0f;
    bool   displayDistance = true;
    bool   displayUnit = true;
};

struct LinearRuler : RulerItemBase {
    QPointF point1;
    QPointF point2;
    QString attachedBrick1Id;   // empty if unattached
    QString attachedBrick2Id;
    float   offsetDistance = 0.0f;
    bool    allowOffset = false;
};

struct CircularRuler : RulerItemBase {
    QPointF center;
    float   radius = 0.0f;
    QString attachedBrickId;
};

}
