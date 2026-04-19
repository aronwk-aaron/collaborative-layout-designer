#pragma once

#include "Ids.h"

#include <memory>
#include <vector>

namespace cld::core {

class Layer;

class Map {
public:
    Map();
    ~Map();
    Map(const Map&) = delete;
    Map& operator=(const Map&) = delete;

    const std::vector<std::unique_ptr<Layer>>& layers() const { return layers_; }
    std::vector<std::unique_ptr<Layer>>& layers() { return layers_; }

private:
    std::vector<std::unique_ptr<Layer>> layers_;
};

}
