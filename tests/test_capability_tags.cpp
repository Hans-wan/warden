#include <gtest/gtest.h>
#include <set>
#include <string>
#include "core/capability/tags.h"

using core::cap::Tag;
using namespace core::cap;

TEST(Tags, PersistentTierIsExactlyTheThreeReadOnlyTags) {
  std::set<Tag> want{Tag::FsRead, Tag::GitRead, Tag::EnvRead};
  for (Tag t : all()) {
    EXPECT_EQ(tierOf(t) == Tier::Persistent, want.count(t) > 0) << name(t);
  }
}

TEST(Tags, SecretsReadIsTurnTier) {
  // Reads, yet it is explicitly placed in the turn tier.
  EXPECT_EQ(tierOf(Tag::SecretsRead), Tier::Turn);
}

TEST(Tags, IsBestEffort) {
  EXPECT_FALSE(isBestEffort(Tag::FsWrite));
  EXPECT_TRUE(isBestEffort(Tag::PkgInstall));
}

TEST(Tags, AllContainsNineteenTags) {
  EXPECT_EQ(all().size(), 19u);
}

TEST(Tags, NameRoundTrip) {
  for (Tag t : all()) {
    auto back = fromName(name(t));
    ASSERT_TRUE(back.has_value()) << name(t);
    EXPECT_EQ(*back, t);
  }
}

TEST(Tags, TagCountMatchesSpec) {
  // 10 core + 9 best-effort
  int core = 0, be = 0;
  for (Tag t : all()) isBestEffort(t) ? ++be : ++core;
  EXPECT_EQ(core, 10);
  EXPECT_EQ(be, 9);
}
