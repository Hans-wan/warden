#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include "core/capability/tags.h"
#include "core/policy/policy.h"

namespace fs = std::filesystem;
using core::cap::Tag;
using core::policy::Mode;
using core::policy::Policy;
using namespace core::policy;

TEST(Policy, EmbeddedDefaultIsObserve) {
  auto p = defaultPolicy();
  EXPECT_EQ(p.def, Mode::Observe);
  EXPECT_EQ(decide(p, Tag::FsDelete), Mode::Ask);
  EXPECT_EQ(decide(p, Tag::FsRead), Mode::Observe);
}

TEST(Policy, ProjectFileOverrides) {
  auto dir = fs::temp_directory_path() / "warden-policy-test";
  fs::create_directories(dir);
  auto path = dir / "policy.yaml";
  {
    std::ofstream f(path);
    f << "default: enforce\ncapabilities:\n  fs:delete: enforce\n";
  }
  auto p = load({path.string()});
  ASSERT_TRUE(p.has_value());
  EXPECT_EQ(p->def, Mode::Enforce);
  EXPECT_EQ(decide(*p, Tag::FsDelete), Mode::Enforce);
  // The factory's other ask entries stay in effect (overlay, not replace)
  EXPECT_EQ(decide(*p, Tag::GitPush), Mode::Ask);
}

TEST(Policy, MissingProjectFileIsNotAnError) {
  // Zero config: a missing file is skipped silently; factory defaults come back.
  auto p = load({"/nonexistent/nope.yaml"});
  ASSERT_TRUE(p.has_value());
  EXPECT_EQ(p->def, defaultPolicy().def);
}

TEST(Policy, UnknownTagInFileIsIgnored) {
  auto dir = fs::temp_directory_path() / "warden-policy-unk";
  fs::create_directories(dir);
  auto path = dir / "policy.yaml";
  {
    std::ofstream f(path);
    f << "default: observe\ncapabilities:\n  not-a-tag: enforce\n";
  }
  auto p = load({path.string()});
  EXPECT_TRUE(p.has_value()) << "an unknown tag must not fail the load";
}

TEST(Policy, UnknownCapabilityFallsBackToDefault) {
  EXPECT_EQ(decide(defaultPolicy(), Tag::Container), Mode::Observe);
}
