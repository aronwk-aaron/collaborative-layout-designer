#pragma once

#include "FontSpec.h"
#include "Layer.h"

#include <QColor>
#include <QPoint>

namespace cld::core {

enum class CellIndexType {
    Letters = 0,
    Numbers = 1,
};

class LayerGrid : public Layer {
public:
    LayerKind kind() const override { return LayerKind::Grid; }

    QColor gridColor     = QColor(0, 0, 0, 128);
    float  gridThickness = 2.0f;

    QColor subGridColor     = QColor(0, 0, 0, 64);
    float  subGridThickness = 1.0f;

    int  gridSizeInStud   = 32;
    int  subDivisionNumber = 4;          // upstream clamps min 2

    bool displayGrid       = true;
    bool displaySubGrid    = true;
    bool displayCellIndex  = false;

    FontSpec cellIndexFont;
    QColor   cellIndexColor = Qt::black;

    CellIndexType cellIndexColumnType = CellIndexType::Letters;
    CellIndexType cellIndexRowType    = CellIndexType::Numbers;
    QPoint        cellIndexCorner{ 0, 0 };
};

}
