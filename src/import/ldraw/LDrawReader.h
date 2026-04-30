#pragma once

#include <QString>

#include <memory>
#include <vector>

namespace bld::core { class Map; }

namespace bld::import {

// One type-1 LDraw subfile reference parsed out of a .ldr / .dat / .mpd file.
// LDraw axes: Y is up, Z is the "forward" axis. We store the raw fields as
// read; conversion to BlueBrick coords happens in toBlueBrickMap().
struct LDrawPartRef {
    int     colorCode = 0;
    double  x = 0, y = 0, z = 0;        // LDU
    double  m[9] = { 1, 0, 0, 0, 1, 0, 0, 0, 1 };  // 3x3 rotation/scale matrix
    QString filename;                    // e.g. "3001.dat"
};

// Inline geometry — line (type 2), triangle (type 3), or quad (type 4).
// Captured so files that inline their geometry (Studio exports, hand-
// authored .ldr snippets) can be rasterized directly when no library
// part resolves. Vertex coords are LDU, same axes as type 1 refs.
// Type 5 (conditional line) is intentionally NOT captured — it only
// draws when adjacent surfaces are separated by an edge, which is
// render-pipeline detail irrelevant to a flat top-down sprite.
struct LDrawPrimitive {
    int    kind = 0;           // 2 = line, 3 = tri, 4 = quad
    int    colorCode = 0;
    double v[4][3] = { {0} };  // up to 4 vertices (x, y, z) each
};

struct LDrawReadResult {
    bool ok = false;
    QString error;
    std::vector<LDrawPartRef>    parts;
    std::vector<LDrawPrimitive>  primitives;
    QString title;  // first comment line (line 0 after leading "0")
};

// Parse an LDraw text file. Only handles top-level line-1 references and
// line-0 comments; geometry primitives (line 2/3/4/5) are skipped. Subfile
// references are NOT resolved — callers map .dat names to part numbers
// separately. Understands .ldr, .dat, and .mpd (takes the first 0 FILE block
// only for .mpd; further blocks will need a more complete parser later).
LDrawReadResult readLDraw(const QString& path);

// Convert a parsed LDraw model into a brand-new core::Map with one brick
// layer, applying the standard BlueBrick conventions:
//   - 20 LDU = 1 stud; we divide positions by 20.
//   - Top-down projection keeps (x, z) and drops y.
//   - Rotation around Y becomes the brick's orientation angle.
//   - Color code maps 1:1 (LDraw and BlueBrick share the palette).
//   - Part file name becomes PartNumber with ".DAT" stripped and upper-cased.
//
// Caveat: this path only renders pieces whose `<id>.<color>.gif` exists
// in the BlueBrickParts library. Use `bakeMeshFromLDraw` (in
// LDrawMeshBuilder.h) for the user-pointed-LDraw-library pipeline that
// renders any part regardless of BlueBrickParts coverage.
std::unique_ptr<core::Map> toBlueBrickMap(const LDrawReadResult& src);

}
