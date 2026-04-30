#pragma once

#include <QRectF>
#include <QString>

namespace bld::core {

// Common base for anything serialized inside a <Bricks>/<Cells>/<Areas>/<Rulers>
// list. Upstream calls this `Layer.LayerItem`.
//
// Serialization in every content layer:
//   <TypeStartTag id="GUID">
//     <DisplayArea>X/Y/Width/Height</DisplayArea>
//     <MyGroup>GUID-or-empty</MyGroup>   (v5+)
//     ... type-specific fields ...
//   </TypeEndTag>
struct LayerItem {
    QString guid;           // id attribute on the start tag
    QRectF  displayArea;    // bounding box in studs
    QString myGroupId;      // empty if not grouped
};

}
