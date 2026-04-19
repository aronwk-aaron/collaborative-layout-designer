#pragma once

#include "LayerItem.h"

#include <QString>

#include <vector>

namespace cld::core {

// Vanilla <Connexion id="GUID"><LinkedTo>GUID-or-empty</LinkedTo></Connexion>.
// We preserve the link GUID as a string; rebuilding the in-memory graph against
// the parts library lands with Phase 2 rendering.
struct ConnectionPoint {
    QString guid;
    QString linkedToId;  // empty means unlinked (free connection)
};

// Vanilla <Brick id="GUID">.
struct Brick : LayerItem {
    QString partNumber;
    float   orientation = 0.0f;                  // degrees
    int     activeConnectionPointIndex = 0;
    float   altitude = 0.0f;                     // v3+
    std::vector<ConnectionPoint> connections;    // count stored as attribute on write
};

}
