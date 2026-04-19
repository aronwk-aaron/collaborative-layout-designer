#pragma once

#include "Geometry.h"
#include "Ids.h"

#include <string>

namespace cld::core {

struct Brick {
    BrickId     id;
    std::string partNumber;
    int         colorCode = 0;
    Transform2D transform;
    double      altitude = 0.0;
};

}
