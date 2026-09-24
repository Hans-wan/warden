// Skill -> tool-call attribution: scope stack + tiered lifecycle.
// Pure-function package: every function operates only on the State passed in —
// no file access, no clock reads.
#pragma once
#include <string>
#include <vector>

#include "core/state/state.h"

namespace core::attr {

// Opens a scope when a skill is invoked.
void enter(core::state::State& st, std::string skill,
           std::vector<std::string> caps, bool persistSession);

// Demotes at turn end (Stop): turn-tier capabilities of non-persist scopes expire.
// The entry itself stays — the skill's instructions are still in context; only
// its capabilities narrow.
void endTurn(core::state::State& st);

// Union of all open scopes' capabilities (used by adjudication).
std::vector<std::string> activeCaps(const core::state::State& st);

// Set of likely responsible skills (rejection report). Multi-scope attribution
// cannot be precise.
std::vector<std::string> responsible(const core::state::State& st);

}  // namespace core::attr
