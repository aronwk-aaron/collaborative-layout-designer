#pragma once

#include <QString>

#include <memory>

namespace cld::core { class Map; }

namespace cld::saveload {

struct LoadResult {
    std::unique_ptr<core::Map> map;
    QString error;
    bool ok() const { return map != nullptr; }
};

// Phase 1 target: load a vanilla BlueBrick .bbm file into core::Map.
// Stub: declared here, implementation lands with the full XML schema port.
LoadResult readBbm(const QString& path);

}
