#pragma once

#include <cstdint>
#include <functional>

namespace cld::core {

template <class Tag>
class Id {
public:
    using Value = std::uint64_t;
    constexpr Id() = default;
    constexpr explicit Id(Value v) : value_(v) {}
    constexpr Value value() const { return value_; }
    constexpr bool operator==(const Id&) const = default;
    constexpr auto operator<=>(const Id&) const = default;

private:
    Value value_ = 0;
};

struct LayerTag {};
struct BrickTag {};
struct GroupTag {};
struct AreaTag {};
struct RulerTag {};
struct LabelTag {};
struct ModuleTag {};

using LayerId  = Id<LayerTag>;
using BrickId  = Id<BrickTag>;
using GroupId  = Id<GroupTag>;
using AreaId   = Id<AreaTag>;
using RulerId  = Id<RulerTag>;
using LabelId  = Id<LabelTag>;
using ModuleId = Id<ModuleTag>;

}

namespace std {
template <class Tag>
struct hash<cld::core::Id<Tag>> {
    size_t operator()(const cld::core::Id<Tag>& id) const noexcept {
        return hash<typename cld::core::Id<Tag>::Value>{}(id.value());
    }
};
}
