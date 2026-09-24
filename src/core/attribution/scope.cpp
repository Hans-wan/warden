#include "core/attribution/scope.h"

#include <algorithm>
#include <unordered_set>
#include <utility>

#include "core/capability/tags.h"

namespace core::attr {

void enter(core::state::State& st, std::string skill,
           std::vector<std::string> caps, bool persistSession) {
  st.scopes.push_back(
      core::state::Scope{std::move(skill), std::move(caps), persistSession});
}

void endTurn(core::state::State& st) {
  for (auto& sc : st.scopes) {
    if (sc.persistSession) continue;
    std::vector<std::string> kept;
    for (const auto& c : sc.caps) {
      auto t = cap::fromName(c);
      // Unknown tags are treated as turn-tier (dropped): degrade conservatively
      // when the state file was hand-edited or carries unknown tags.
      if (t && cap::tierOf(*t) == cap::Tier::Persistent) kept.push_back(c);
    }
    sc.caps = std::move(kept);
  }
}

std::vector<std::string> activeCaps(const core::state::State& st) {
  std::unordered_set<std::string> seen;
  std::vector<std::string> out;
  for (const auto& sc : st.scopes)
    for (const auto& c : sc.caps)
      if (seen.insert(c).second) out.push_back(c);
  return out;
}

std::vector<std::string> responsible(const core::state::State& st) {
  std::vector<std::string> out;
  for (const auto& sc : st.scopes) out.push_back(sc.skill);
  std::sort(out.begin(), out.end());
  return out;
}

}  // namespace core::attr
