#pragma once

#include "ColorSpec.h"
#include "FontSpec.h"
#include "LayerItem.h"

#include <QPointF>
#include <QString>

#include <vector>

namespace bld::core {

enum class RulerKind {
    Linear,
    Circular,
};

// Fields common to every RulerItem subtype (matches upstream RulerItem base class
// in LayerRulerItem.cs line 318-341).
struct RulerItemBase : LayerItem {
    ColorSpec color = ColorSpec::fromKnown(QColor(Qt::black), QStringLiteral("Black"));
    float     lineThickness = 1.0f;
    bool      displayDistance = true;
    bool      displayUnit = true;
    ColorSpec guidelineColor = ColorSpec::fromKnown(QColor(Qt::black), QStringLiteral("Black"));
    float     guidelineThickness = 1.0f;
    std::vector<float> guidelineDashPattern;
    int       unit = 0;              // Tools.Distance.Unit enum as int
    FontSpec  measureFont;
    ColorSpec measureFontColor = ColorSpec::fromKnown(QColor(Qt::black), QStringLiteral("Black"));
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
