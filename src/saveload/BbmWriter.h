#pragma once

#include <QString>

namespace cld::core { class Map; }

class QIODevice;

namespace cld::saveload {

struct WriteResult {
    bool ok = false;
    QString error;
};

// Write a core::Map to a vanilla-compatible .bbm file. Phase 1 writes the map
// header + empty <Layers/>. Layer serialization arrives in subsequent commits.
WriteResult writeBbm(const core::Map& map, const QString& path);
WriteResult writeBbm(const core::Map& map, QIODevice& output);

}
