#pragma once

#include "FontSpec.h"
#include "LayerItem.h"

#include <QColor>
#include <QString>

namespace cld::core {

enum class TextAlignment {
    Near,
    Center,
    Far,
};

struct TextCell : LayerItem {
    QString       text;
    float         orientation = 0.0f;
    QColor        fontColor = Qt::black;
    FontSpec      font;
    TextAlignment alignment = TextAlignment::Center;
};

}
