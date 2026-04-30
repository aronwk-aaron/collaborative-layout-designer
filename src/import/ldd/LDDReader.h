#pragma once

#include "../ldraw/LDrawReader.h"

#include <QString>

namespace bld::import {

// LDD (LEGO Digital Designer) reader. Consumes either:
//   - `.lxfml` — the raw LXFML XML document.
//   - `.lxf`   — a ZIP archive containing an LXFML entry (plus thumbnails).
//
// LXFML (<LXFML>/<Bricks>/<Brick designID=.../> with <Bone transformation="..">)
// is close enough to LDraw's geometry model that we map into the shared
// `LDrawReadResult` type and let `toBlueBrickMap()` handle the rest.
//
// Part numbering: LDD's designID and material id gets flattened to
// "<designID>.<materialId>" to match BlueBrickParts' naming convention
// where available. Unknown parts get the raw designID as the part
// number; they'll miss in the library lookup and simply contribute
// nothing to the sprite.
//
// LDD coordinates: 1 LDD "stud" = 0.8 mm × 0.8 mm × 0.96 mm (brick
// unit is 8mm x 8mm x 9.6mm — one LDD unit per mm/1.25). We convert
// into LDraw LDU (1 LDU ≈ 0.4 mm = 1/2 stud), so `toBlueBrickMap` can
// apply its existing 20-LDU-per-stud rule.
LDrawReadResult readLDD(const QString& path);

}  // namespace bld::import
