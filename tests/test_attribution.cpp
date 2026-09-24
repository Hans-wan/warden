#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

#include "core/attribution/scope.h"
#include "core/state/state.h"

using core::state::State;
using namespace core::attr;

static bool has(const std::vector<std::string>& v, const std::string& s) {
  return std::find(v.begin(), v.end(), s) != v.end();
}

TEST(Attribution, EnterAndActiveCaps) {
  State st; st.sessionId = "s";
  enter(st, "pdf-tools", {"fs:read", "shell"}, false);
  enter(st, "data-crunch", {"fs:read"}, false);

  auto got = activeCaps(st);
  EXPECT_TRUE(has(got, "fs:read"));
  EXPECT_TRUE(has(got, "shell"));
}

TEST(Attribution, EndTurnExpiresTurnTierCaps) {
  State st; st.sessionId = "s";
  enter(st, "pdf-tools", {"fs:read", "shell"}, false);  // fs:read persists, shell is turn-tier
  endTurn(st);

  auto got = activeCaps(st);
  EXPECT_TRUE(has(got, "fs:read")) << "persistent tier should survive";
  EXPECT_FALSE(has(got, "shell")) << "turn tier should expire at turn end";
}

TEST(Attribution, PersistSessionKeepsTurnTierCaps) {
  State st; st.sessionId = "s";
  enter(st, "writer", {"fs:write"}, true);
  endTurn(st);
  EXPECT_TRUE(has(activeCaps(st), "fs:write"));
}

TEST(Attribution, ResponsibleListsOpenScopes) {
  State st; st.sessionId = "s";
  enter(st, "pdf-tools", {}, false);
  enter(st, "data-crunch", {}, false);

  auto got = responsible(st);
  std::sort(got.begin(), got.end());
  EXPECT_EQ(got, (std::vector<std::string>{"data-crunch", "pdf-tools"}));
}

TEST(Attribution, NoScopesYieldsEmptyUnion) {
  State st; st.sessionId = "s";
  EXPECT_TRUE(activeCaps(st).empty());
}

TEST(Attribution, DuplicateCapsNotDuplicatedInUnion) {
  State st; st.sessionId = "s";
  enter(st, "a", {"fs:read"}, false);
  enter(st, "b", {"fs:read"}, false);
  EXPECT_EQ(activeCaps(st).size(), 1u);
}

TEST(Attribution, EndTurnKeepsScopeEntryItself) {
  // Demotion is not popping: the skill's instructions stay in context; only its capabilities narrow.
  State st; st.sessionId = "s";
  enter(st, "pdf-tools", {"fs:write"}, false);
  endTurn(st);
  EXPECT_EQ(st.scopes.size(), 1u);  // the entry is still there
  EXPECT_TRUE(st.scopes[0].caps.empty());  // but its turn-tier capabilities have expired
}

TEST(Attribution, EndTurnDropsUnknownTags) {
  // Unknown tags (fromName returns nullopt) are treated as turn-tier — the
  // conservative direction: drop.
  State st; st.sessionId = "s";
  enter(st, "pdf-tools", {"fs:read", "bogus:tag"}, false);
  endTurn(st);
  auto got = activeCaps(st);
  EXPECT_TRUE(has(got, "fs:read")) << "known persistent tags should survive";
  EXPECT_FALSE(has(got, "bogus:tag")) << "unknown tags should be dropped as turn-tier";
}
