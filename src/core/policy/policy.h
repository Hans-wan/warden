// Two-level policy: compile-time-embedded factory defaults + optional project override file.
#pragma once
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "core/capability/tags.h"

namespace core::policy {

enum class Mode { Observe, Ask, Enforce };

struct Policy {
  Mode def = Mode::Observe;
  std::map<cap::Tag, Mode> capModes;
};

// Parses the compile-time-embedded factory defaults.
Policy defaultPolicy();

// Merges in order; nonexistent paths are skipped silently; a corrupt one returns nullopt.
std::optional<Policy> load(const std::vector<std::string>& paths);

// Adjudication: table lookup first, falling back to default on a miss. Pure function.
Mode decide(const Policy& p, cap::Tag t);

}  // namespace core::policy
