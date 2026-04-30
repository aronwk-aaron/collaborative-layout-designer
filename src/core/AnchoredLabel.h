#pragma once

#include "ColorSpec.h"
#include "FontSpec.h"

#include <QPointF>
#include <QString>

namespace bld::core {

enum class AnchorKind {
    World,   // offset is in world studs (absolute position)
    Brick,   // offset is in target-brick local coords; moves with the brick
    Group,   // anchored to an upstream Group
    Module,  // anchored to a cross-layer Module (fork-only)
};

// Fork-only: text that sticks to its anchor when the anchor moves or rotates.
// Lives in the .bbm.bld sidecar so vanilla BlueBrick still opens the .bbm
// cleanly (no new element kinds inside the vanilla XML).
struct AnchoredLabel {
    QString    id;               // sidecar-scope unique identifier (UUID)
    QString    text;
    FontSpec   font;
    ColorSpec  color;
    AnchorKind kind = AnchorKind::World;
    QString    targetId;         // brick.guid / group.guid / module.id; ignored for World
    QPointF    offset;           // in target's local coord system (studs)
    float      offsetRotation = 0.0f;  // degrees
    double     minZoom = 0.0;    // optional visibility cutoff (0 = always visible)
};

}
