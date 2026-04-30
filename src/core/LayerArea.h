#pragma once

#include "Layer.h"

#include <QColor>

#include <vector>

namespace bld::core {

// Upstream LayerArea is a sparse 2D grid of colored cells keyed on (x, y).
// We preserve raw wire-format entries so round-trip writes emit identical XML,
// regardless of map ordering. Phase 2 rendering converts this to a QHash for
// fast cell lookup.
struct AreaCell {
    int    x = 0;
    int    y = 0;
    QColor color = Qt::transparent;
};

class LayerArea : public Layer {
public:
    LayerKind kind() const override { return LayerKind::Area; }

    int areaCellSizeInStud = 32;
    std::vector<AreaCell> cells;
};

}
