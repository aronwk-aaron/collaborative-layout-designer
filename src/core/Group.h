#pragma once

#include "LayerItem.h"

#include <QString>

namespace bld::core {

// Vanilla upstream <Group id="GUID"><PartNumber/><MyGroup/></Group>.
// Groups are written after all items in a layer, as a flat <Groups> list,
// and linked by GUID via each item's <MyGroup> element.
struct Group : LayerItem {
    QString partNumber;  // empty for user-created groups, non-empty for library meta-groups
};

}
