// Best-effort detection: regex matching, guaranteed incomplete. Docs must say
// best-effort tier.
#pragma once
#include <string_view>
#include <vector>
#include "core/capability/tags.h"

namespace core::cap {
// Returns tags hit by the best-effort set. Union with the fromBash result.
std::vector<Tag> bestEffortExtra(std::string_view command);
}  // namespace core::cap
