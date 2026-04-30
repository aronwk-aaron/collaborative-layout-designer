#pragma once

#include "ColorSpec.h"
#include "FontSpec.h"
#include "LayerItem.h"

#include <QString>

namespace bld::core {

enum class TextAlignment {
    Near,
    Center,
    Far,
};

struct TextCell : LayerItem {
    QString       text;
    float         orientation = 0.0f;
    ColorSpec     fontColor = ColorSpec::fromKnown(QColor(Qt::black), QStringLiteral("Black"));
    FontSpec      font;
    TextAlignment alignment = TextAlignment::Center;
};

}
