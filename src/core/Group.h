#pragma once

#include "Ids.h"

#include <unordered_set>
#include <variant>

namespace cld::core {

using MemberId = std::variant<BrickId, RulerId, AreaId, LabelId, GroupId>;

struct MemberIdHash {
    size_t operator()(const MemberId& m) const noexcept {
        return std::visit([](auto id) { return std::hash<decltype(id)>{}(id); }, m);
    }
};

struct Group {
    GroupId id;
    std::unordered_set<MemberId, MemberIdHash> members;
};

}
