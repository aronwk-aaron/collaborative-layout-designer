#pragma once

#include "../ldraw/LDrawReader.h"

#include <QString>

namespace bld::import {

// Studio 2.0 `.io` files are ZIP archives containing (at minimum) a
// `model.ldr` — same LDraw text format as plain `.ldr` — plus assorted
// metadata / previews / build-step images. For BLD's purposes we only
// need model.ldr; everything else is discarded.
//
// Returns the same LDrawReadResult as readLDraw(). If the archive has
// no model.ldr or isn't a valid ZIP, `ok = false` and `error` explains.
LDrawReadResult readStudioIo(const QString& path);

}  // namespace bld::import
