// Bash command string -> capability tags. Pure function, no I/O.
#pragma once
#include <string_view>
#include <vector>
#include "core/capability/tags.h"

namespace core::cap {
std::vector<Tag> fromBash(std::string_view command);
}  // namespace core::cap
