#pragma once

#include "Group.h"
#include "Layer.h"
#include "TextCell.h"

#include <vector>

namespace bld::core {

class LayerText : public Layer {
public:
    LayerKind kind() const override { return LayerKind::Text; }

    std::vector<TextCell> textCells;
    std::vector<Group>    groups;
};

}
