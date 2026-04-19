#pragma once

#include <QString>

namespace cld::core { class Map; }

namespace cld::saveload {

struct WriteResult {
    bool ok = false;
    QString error;
};

// Phase 1 target: write a core::Map out as a vanilla-compatible .bbm file.
// Fork-only metadata goes to a sidecar .bbm.cld (see SidecarIO).
WriteResult writeBbm(const core::Map& map, const QString& path);

}
