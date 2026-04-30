#pragma once

#include "../../geom/Mesh.h"

#include <QImage>
#include <QPointF>
#include <QRectF>

namespace bld::import {

// Rasterize a geom::Mesh into a top-down (drop-Y) sprite suitable for
// emitting as a BlueBrick part image.
//
// Coordinate convention:
//   * The mesh is in LDraw-equivalent units. 20 LDU = 1 stud (the
//     BlueBrick / vanilla convention).
//   * Output `pxPerStud` defaults to 8 (BlueBrick's sampling rate).
//   * "Top-down" means we drop Y (LDraw up-axis) and project (x, z).
//     Higher-Y triangles draw on top of lower-Y triangles.
//   * Image size auto-fits the mesh's XZ bounding box, with a 1-pixel
//     margin on each side so anti-aliased edges don't get clipped.
//
// Implementation:
//   * Sort triangles by maximum Y ascending so the painter naturally
//     paints higher pieces last. Equivalent to a stable depth sort
//     for orthographic top-down.
//   * QPainter with antialiasing for smooth edges. Each tri is filled
//     with its baked colour; transparent colours blend correctly via
//     QPainter's source-over composite.
//   * We don't render LDraw type-2 / type-5 edge lines — fills are
//     enough for top-down sprites and the line files reference
//     non-fill primitives that don't affect colour mass.
struct RasterizeOptions {
    int     pxPerStud = 8;
    double  studsPerLdu = 1.0 / 20.0;     // 20 LDU per stud
    int     marginPx = 2;
    bool    antialias = true;

    // Supersampling factor: rasterise at (pxPerStud * ssaa) internally,
    // then smooth-downscale to pxPerStud. ssaa=4 is a good quality/cost
    // sweet spot — visibly cleaner edges than QPainter AA alone, while
    // 16× memory/time stays cheap for typical part sprites.
    int     ssaa = 4;

    // Stroke the mesh's `edges` (LDraw type-2 wireframe lines) over
    // the filled triangles. Without this, flat baseplates and plate
    // tops render as featureless colour blobs — the stud outlines,
    // brick seams, and embossed-print silhouettes all live in the
    // type-2 lines. With this on, the sprite reads as a real LEGO
    // top-down rather than a flat fill.
    bool    wireframe = false;
};

struct RasterizeResult {
    QImage  image;
    QRectF  meshBoundsXZ;     // pre-scale, in stud units (handy for caller layout)
    QPointF imageOriginInStuds;  // stud coord of the image's (0, 0) pixel
};

RasterizeResult rasterizeMeshTopDown(const geom::Mesh& mesh,
                                      const RasterizeOptions& opt = {});

}  // namespace bld::import
