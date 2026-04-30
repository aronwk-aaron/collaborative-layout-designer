#pragma once

#include <QString>

#include <chrono>
#include <cstdint>
#include <functional>
#include <random>

namespace bld::core {

// Vanilla BlueBrick stores every item GUID (layer ids, brick ids, group ids,
// ruler ids, connection point ids) as a decimal-formatted 64-bit unsigned
// integer — SaveLoadManager.UniqueId's ctor calls `ulong.Parse(string)` on
// load, which throws FormatException on anything that's not a bare decimal
// number. Our edit commands must use this helper to mint new IDs so files
// saved by the fork open cleanly in vanilla BlueBrick 1.9.2.
//
// The ID is random (128-bit collision probability is overkill but we only
// have 64 bits to fit into ulong; collisions across a single project are
// astronomically unlikely at 2^-32 birthday bounds for a few thousand items).
inline QString newBbmId() {
    static thread_local std::mt19937_64 rng{
        std::random_device{}() ^
        static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count())
    };
    // Avoid 0 (Empty) and keep below 2^63 to leave room for int64 readers.
    std::uint64_t v = 0;
    while (v == 0) v = rng() & 0x7FFFFFFFFFFFFFFFULL;
    return QString::number(v);
}

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
struct hash<bld::core::Id<Tag>> {
    size_t operator()(const bld::core::Id<Tag>& id) const noexcept {
        return hash<typename bld::core::Id<Tag>::Value>{}(id.value());
    }
};
}
