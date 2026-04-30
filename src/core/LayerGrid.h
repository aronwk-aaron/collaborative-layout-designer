#pragma once

#include "ColorSpec.h"
#include "FontSpec.h"
#include "Layer.h"

#include <QPoint>

namespace bld::core {

enum class CellIndexType {
    Letters = 0,
    Numbers = 1,
};

class LayerGrid : public Layer {
public:
    LayerKind kind() const override { return LayerKind::Grid; }

    ColorSpec gridColor     = ColorSpec::fromArgb(QColor(0, 0, 0, 128));
    float     gridThickness = 2.0f;

    ColorSpec subGridColor     = ColorSpec::fromArgb(QColor(0, 0, 0, 64));
    float     subGridThickness = 1.0f;

    int  gridSizeInStud    = 32;
    int  subDivisionNumber = 4;          // upstream clamps min 2

    bool displayGrid       = true;
    bool displaySubGrid    = true;
    bool displayCellIndex  = false;

    FontSpec cellIndexFont;
    ColorSpec cellIndexColor = ColorSpec::fromKnown(QColor(Qt::black), QStringLiteral("Black"));

    CellIndexType cellIndexColumnType = CellIndexType::Letters;
    CellIndexType cellIndexRowType    = CellIndexType::Numbers;
    QPoint        cellIndexCorner{ 0, 0 };
};

}
