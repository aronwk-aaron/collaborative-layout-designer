#pragma once

namespace bld::core {

struct PointMm {
    double x = 0.0;
    double y = 0.0;
};

struct SizeMm {
    double w = 0.0;
    double h = 0.0;
};

struct RectMm {
    PointMm origin;
    SizeMm  size;
};

struct Transform2D {
    PointMm translation;
    double  rotationDegrees = 0.0;
};

constexpr double kPxPerStud = 8.0;
constexpr double kMmPerStud = 8.0;

}
