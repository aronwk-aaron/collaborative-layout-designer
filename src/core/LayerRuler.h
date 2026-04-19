#pragma once

#include "Group.h"
#include "Layer.h"
#include "RulerItem.h"

#include <memory>
#include <vector>

namespace cld::core {

class LayerRuler : public Layer {
public:
    LayerKind kind() const override { return LayerKind::Ruler; }

    // Items are heterogeneous (LinearRuler or CircularRuler) — upstream uses a
    // polymorphic base class. We store via unique_ptr<RulerItemBase> but RulerItemBase
    // is not polymorphic at the C++ level; we tag with RulerKind to dispatch.
    struct AnyRuler {
        RulerKind kind = RulerKind::Linear;
        // Only one of these is populated per kind; trivially constructible so we
        // keep both allocated.
        LinearRuler   linear;
        CircularRuler circular;
    };

    std::vector<AnyRuler> rulers;
    std::vector<Group>    groups;
};

}
