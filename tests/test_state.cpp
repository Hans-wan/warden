#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

#include "core/state/state.h"

namespace fs = std::filesystem;
using core::state::Scope;
using core::state::State;

TEST(State, LoadMissingFileReturnsZeroValue) {
  auto dir = fs::temp_directory_path() / "warden-test-missing";
  std::filesystem::remove_all(dir);
  auto st = core::state::load(dir.string(), "sess-1");
  EXPECT_TRUE(st.has_value());
  EXPECT_TRUE(st->scopes.empty());
}

TEST(State, SaveThenLoadRoundTrip) {
  auto dir = fs::temp_directory_path() / "warden-test-rt";
  std::filesystem::remove_all(dir);
  State want;
  want.sessionId = "s1";
  want.scopes = {
      Scope{"pdf-tools", {"fs:read", "shell"}, false},
      Scope{"writer", {"fs:write"}, true},
  };
  ASSERT_TRUE(core::state::save(dir.string(), "s1", want));
  auto got = core::state::load(dir.string(), "s1");
  ASSERT_TRUE(got.has_value());
  ASSERT_EQ(got->scopes.size(), 2u);
  EXPECT_EQ(got->scopes[0].skill, "pdf-tools");
  EXPECT_TRUE(got->scopes[1].persistSession);
}

TEST(State, SaveIsAtomicNoTempLeftBehind) {
  auto dir = fs::temp_directory_path() / "warden-test-atomic";
  std::filesystem::remove_all(dir);
  State s;
  s.sessionId = "x";
  ASSERT_TRUE(core::state::save(dir.string(), "x", s));
  auto stateDir = core::state::dir(dir.string());
  for (auto const& e : std::filesystem::directory_iterator(stateDir))
    EXPECT_EQ(e.path().extension().string(), ".json") << e.path();
}

TEST(State, DifferentSessionsAreIsolated) {
  auto dir = fs::temp_directory_path() / "warden-test-iso";
  std::filesystem::remove_all(dir);
  State a;
  a.sessionId = "a";
  a.scopes.push_back(Scope{"x", {}, false});
  ASSERT_TRUE(core::state::save(dir.string(), "a", a));
  auto b = core::state::load(dir.string(), "b");
  ASSERT_TRUE(b.has_value());
  EXPECT_TRUE(b->scopes.empty());
}

// Final safety net: splicing sessionId straight into a path lets "../../evil"
// escape .warden/state.
TEST(State, IllegalSessionIdRejected) {
  auto dir = fs::temp_directory_path() / "warden-test-escape";
  std::filesystem::remove_all(dir);
  State s;
  s.sessionId = "../../evil";
  EXPECT_FALSE(core::state::save(dir.string(), "../../evil", s))
      << "an illegal session_id must not be persisted";
  EXPECT_FALSE(fs::exists(dir / ".warden" / "evil.json"));
  auto got = core::state::load(dir.string(), "../../evil");
  ASSERT_TRUE(got.has_value()) << "an illegal sid reads as no state (fail-safe)";
  EXPECT_TRUE(got->scopes.empty());
  std::filesystem::remove_all(dir);
}

TEST(State, CorruptFileReturnsNullopt) {
  auto dir = fs::temp_directory_path() / "warden-test-corrupt";
  std::filesystem::remove_all(dir);
  std::error_code ec;
  std::filesystem::create_directories(core::state::dir(dir.string()), ec);
  {
    std::ofstream out(core::state::dir(dir.string()) + "/bad.json",
                      std::ios::trunc);
    out << "{ not json";
  }
  auto st = core::state::load(dir.string(), "bad");
  EXPECT_FALSE(st.has_value());
}

TEST(State, CountersRoundTrip) {
  auto dir = fs::temp_directory_path() / "warden-test-counters";
  std::filesystem::remove_all(dir);
  State s;
  s.sessionId = "s1";
  s.counters.allow = 87;
  s.counters.ask = 1;
  s.counters.deny = 2;
  ASSERT_TRUE(core::state::save(dir.string(), "s1", s));
  auto got = core::state::load(dir.string(), "s1");
  ASSERT_TRUE(got.has_value());
  EXPECT_EQ(got->counters.allow, 87u);
  EXPECT_EQ(got->counters.ask, 1u);
  EXPECT_EQ(got->counters.deny, 2u);
  EXPECT_FALSE(got->lastDenial.has_value());
}

TEST(State, LastDenialRoundTrip) {
  auto dir = fs::temp_directory_path() / "warden-test-lastdenial";
  std::filesystem::remove_all(dir);
  State s;
  s.sessionId = "s1";
  s.lastDenial = core::state::LastDenial{"bash", "rm -rf tmp/", "fs:delete", "2026-09-23T10:00:00Z"};
  ASSERT_TRUE(core::state::save(dir.string(), "s1", s));
  auto got = core::state::load(dir.string(), "s1");
  ASSERT_TRUE(got.has_value());
  ASSERT_TRUE(got->lastDenial.has_value());
  EXPECT_EQ(got->lastDenial->tool, "bash");
  EXPECT_EQ(got->lastDenial->summary, "rm -rf tmp/");
  EXPECT_EQ(got->lastDenial->caps, "fs:delete");
  EXPECT_EQ(got->lastDenial->t, "2026-09-23T10:00:00Z");
}

TEST(State, OldFileWithoutCountersLoadsAsDefaults) {
  auto dir = fs::temp_directory_path() / "warden-test-oldstate";
  std::filesystem::remove_all(dir);
  std::filesystem::create_directories(core::state::dir(dir.string()));
  std::ofstream out(core::state::dir(dir.string()) + "/old.json");
  out << R"({"session_id":"old","scopes":[]})";
  out.close();
  auto got = core::state::load(dir.string(), "old");
  ASSERT_TRUE(got.has_value());
  EXPECT_EQ(got->counters.allow, 0u);
  EXPECT_FALSE(got->lastDenial.has_value());
}
