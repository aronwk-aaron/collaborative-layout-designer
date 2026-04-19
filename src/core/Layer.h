#pragma once

#include "ColorSpec.h"
#include "Ids.h"

#include <QString>

namespace cld::core {

enum class LayerKind {
    Grid,
    Brick,
    Text,
    Area,
    Ruler,
    AnchoredText,  // fork-only, stored in sidecar
};

struct HullProperties {
    bool      displayHulls = false;
    ColorSpec color;
    int       thickness = 1;
};

class Layer {
public:
    virtual ~Layer() = default;

    virtual LayerKind kind() const = 0;

    // Mirrors upstream's <Layer type="..." id="..."> attributes + Name/Visible/Transparency.
    QString guid;              // upstream SaveLoadManager.UniqueId rendered as string
    QString name;
    bool    visible = true;
    int     transparency = 100;  // percent, v5+ upstream
    HullProperties hull;         // v9+ upstream

protected:
    Layer() = default;
};

}
