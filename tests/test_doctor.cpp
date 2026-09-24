#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>

#include "doctor/doctor.h"

namespace fs = std::filesystem;
using core::doctor::Check;

TEST(Doctor, ReturnsAllChecks) {
  auto dir = fs::temp_directory_path() / "warden-doctor-test";
  fs::remove_all(dir);
  fs::create_directories(dir / "hooks");
  {
    std::ofstream f(dir / "hooks" / "hooks.json");
    f << R"({"hooks":{"SessionStart":{},"PreToolUse":{},"PostToolUse":{},"Stop":{}}})";
  }
  auto checks = core::doctor::run(dir.string(), "/nonexistent/warden");
  EXPECT_GE(checks.size(), 4u);
  for (auto& c : checks) EXPECT_FALSE(c.name.empty());
}

TEST(Doctor, HooksConfiguredFailsWhenFileMissing) {
  auto checks = core::doctor::run(
      (fs::temp_directory_path() / "warden-doctor-none").string(), "/nonexistent");
  for (auto& c : checks)
    if (c.name == "hooks-configured")
      EXPECT_FALSE(c.ok) << "with hooks.json missing this check must fail (silent failure is the biggest risk)";
}

// Extra case: with all four events present hooks-configured must pass (guarding
// against the previous case's always-fail check passing a fake green).
TEST(Doctor, HooksConfiguredOkWithAllFourEvents) {
  auto dir = fs::temp_directory_path() / "warden-doctor-ok";
  fs::remove_all(dir);
  fs::create_directories(dir / "hooks");
  {
    std::ofstream f(dir / "hooks" / "hooks.json");
    f << R"({"hooks":{"SessionStart":[{"command":"warden session-start"}],)"
         R"("PreToolUse":[{"command":"warden pre-tool"}],)"
         R"("PostToolUse":[{"command":"warden post-tool"}],)"
         R"("Stop":[{"command":"warden stop"}]}})";
  }
  auto checks = core::doctor::run(dir.string(), "/nonexistent/warden");
  bool saw = false;
  for (auto& c : checks) {
    if (c.name != "hooks-configured") continue;
    saw = true;
    EXPECT_TRUE(c.ok) << c.note;
  }
  EXPECT_TRUE(saw) << "the hooks-configured check must exist";
}

// Extra case: doctor must not crash on a missing binary, and hook-fires/latency must report failure.
TEST(Doctor, MissingBinaryFailsFireAndLatencyWithoutCrash) {
  auto dir = fs::temp_directory_path() / "warden-doctor-nobin";
  fs::remove_all(dir);
  fs::create_directories(dir);
  auto checks = core::doctor::run(dir.string(), "/nonexistent/warden");
  for (auto& c : checks) {
    if (c.name == "hook-fires" || c.name == "hot-path-latency") {
      EXPECT_FALSE(c.ok) << c.name << ": " << c.note;
    }
  }
}

// Locates the real warden binary built next to this test executable. ctest
// invokes the test binaries with absolute paths, so argv[0]'s directory is the
// build directory regardless of the caller's cwd; "" means "not found" and the
// caller skips instead of failing, which keeps the suite hermetic when the test
// binary is moved away from the build tree.
std::string builtWardenPath() {
  const auto& args = ::testing::internal::GetArgvs();
  if (args.empty()) return "";
  const fs::path candidate = fs::path(args[0]).parent_path() / "warden";
  std::error_code ec;
  if (fs::exists(candidate, ec)) return candidate.string();
  return "";
}

// The fifth check must be present in run()'s output. Any root works: presence
// does not depend on the environment.
TEST(DoctorStatusline, CheckAppearsInRunOutput) {
  auto dir = fs::temp_directory_path() / "warden-doctor-sl";
  fs::remove_all(dir);
  fs::create_directories(dir / "hooks");
  {
    std::ofstream f(dir / "hooks" / "hooks.json");
    f << "{}";
  }
  auto checks = core::doctor::run(dir.string(), "/nonexistent/warden");
  bool found = false;
  for (const auto& c : checks) {
    if (c.name == "statusline-usable") found = true;
  }
  EXPECT_TRUE(found) << "statusline-usable must always appear in the doctor report";
}

// Extra case: a missing binary must fail the check, not crash doctor and not
// fake a green. This is the same failure shape hook-fires reports for a wrong
// path (doctor's most important silent-failure guard).
TEST(DoctorStatusline, FailsWithoutCrashOnMissingBinary) {
  auto dir = fs::temp_directory_path() / "warden-doctor-sl-nobin";
  fs::remove_all(dir);
  fs::create_directories(dir);
  auto checks = core::doctor::run(dir.string(), "/nonexistent/warden");
  bool saw = false;
  for (auto& c : checks) {
    if (c.name != "statusline-usable") continue;
    saw = true;
    EXPECT_FALSE(c.ok) << "a missing binary must fail the statusline check";
  }
  EXPECT_TRUE(saw) << "the statusline-usable check must exist";
}

// Extra case: with the real binary the check must pass — otherwise the
// always-fail case above could be green-by-construction.
TEST(DoctorStatusline, PassesWithBuiltBinary) {
  const std::string bin = builtWardenPath();
  if (bin.empty()) GTEST_SKIP() << "warden binary not found beside the test executable";
  auto dir = fs::temp_directory_path() / "warden-doctor-sl-ok";
  fs::remove_all(dir);
  fs::create_directories(dir / "hooks");
  {
    std::ofstream f(dir / "hooks" / "hooks.json");
    f << R"({"hooks":{"SessionStart":{},"PreToolUse":{},"PostToolUse":{},"Stop":{}}})";
  }
  auto checks = core::doctor::run(dir.string(), bin);
  bool saw = false;
  for (auto& c : checks) {
    if (c.name != "statusline-usable") continue;
    saw = true;
    EXPECT_TRUE(c.ok) << c.note;
  }
  EXPECT_TRUE(saw) << "the statusline-usable check must exist";
}

// The plugin-layout walk: hooks/hooks.json one level above the binary's
// directory is the shipped layout (<plugin>/bin/warden), so doctor can
// self-locate the plugin root.
TEST(DoctorPluginRoot, FindsLayoutAboveBinary) {
  auto dir = fs::temp_directory_path() / "warden-doctor-pluginroot";
  fs::remove_all(dir);
  fs::create_directories(dir / "bin");
  fs::create_directories(dir / "hooks");
  {
    std::ofstream f(dir / "hooks" / "hooks.json");
    f << "{}";
  }
  EXPECT_EQ(core::doctor::findPluginRootPublic((dir / "bin" / "warden").string()),
            dir.string());
}

// No marker anywhere in reach: the walk must give up ("" -> doctor keeps using
// the root argument). The fixture sits deeper than the walk's depth budget so
// ancestors we do not control can never supply a marker. Coupled to
// findPluginRoot's budget being exactly 6 (doctor.cpp): this fixture is 5
// levels deep, so a larger budget could climb out of it — re-verify the
// fixture's isolation if that constant ever changes.
TEST(DoctorPluginRoot, EmptyWhenLayoutAbsent) {
  auto dir = fs::temp_directory_path() / "warden-doctor-pluginroot-none";
  fs::remove_all(dir);
  fs::create_directories(dir / "bin" / "1" / "2" / "3" / "4");
  EXPECT_EQ(
      core::doctor::findPluginRootPublic((dir / "bin/1/2/3/4/warden").string()), "");
}
