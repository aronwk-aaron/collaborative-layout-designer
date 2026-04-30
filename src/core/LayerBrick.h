#pragma once

#include "Brick.h"
#include "Group.h"
#include "Layer.h"

#include <vector>

namespace bld::core {

class LayerBrick : public Layer {
public:
    LayerKind kind() const override { return LayerKind::Brick; }

    bool displayBrickElevation = false;   // v9+ upstream
    std::vector<Brick> bricks;
    std::vector<Group> groups;
};

}
