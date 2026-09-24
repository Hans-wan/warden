#include <gtest/gtest.h>
#include "core/journal/journal.h"
#include "core/replay/diff.h"

using core::journal::Entry;
using core::replay::Report;

static Entry mkEntry(const std::string& tool, std::vector<std::string> caps) {
  Entry e;
  e.ev = "tool"; e.tool = tool; e.caps = std::move(caps);
  e.decision = "allow";
  return e;
}

TEST(Diff, IdenticalTrajectoriesAreClean) {
  std::vector<Entry> base = {mkEntry("bash", {"shell"}), mkEntry("write", {"fs:write"})};
  auto rep = core::replay::diff(base, base);
  EXPECT_EQ(rep.verdict, "clean");
  EXPECT_TRUE(rep.addedCaps.empty());
}

TEST(Diff, NewCapabilityIsCapsChanged) {
  std::vector<Entry> base = {mkEntry("bash", {"shell"})};
  std::vector<Entry> now = {mkEntry("bash", {"shell"}), mkEntry("curl", {"shell", "net:fetch"})};
  auto rep = core::replay::diff(base, now);
  EXPECT_EQ(rep.verdict, "caps-changed");
  ASSERT_EQ(rep.addedCaps.size(), 1u);
  EXPECT_EQ(rep.addedCaps[0], "net:fetch");
}

TEST(Diff, RemovedCapabilityReportedButNotCapsChanged) {
  std::vector<Entry> base = {mkEntry("curl", {"net:fetch"})};
  std::vector<Entry> now = {mkEntry("ls", {"shell"})};
  auto rep = core::replay::diff(base, now);
  ASSERT_EQ(rep.removedCaps.size(), 1u);
  EXPECT_EQ(rep.removedCaps[0], "net:fetch");
  EXPECT_EQ(rep.verdict, "drift") << "removal without additions must be drift";
}

TEST(Diff, ToolSeqDistanceCountsEdits) {
  std::vector<Entry> base = {mkEntry("read", {}), mkEntry("read", {}), mkEntry("write", {})};
  std::vector<Entry> other = {mkEntry("read", {}), mkEntry("bash", {}), mkEntry("write", {})};
  EXPECT_EQ(core::replay::diff(base, other).toolSeqDist, 1);
}

TEST(Diff, ProductChangeDetectsHashDifference) {
  auto writeEntry = [](const char* hash) {
    Entry e; e.ev = "tool"; e.tool = "write";
    e.input = support::Json::object{
        {"file_path", "/a.txt"}, {"content_sha256", hash}, {"bytes", support::Json(3.0)}};
    return e;
  };
  std::vector<Entry> base = {writeEntry("aaa")};
  std::vector<Entry> now = {writeEntry("bbb")};
  auto rep = core::replay::diff(base, now);
  EXPECT_EQ(rep.productChanges.size(), 1u);
}

TEST(Diff, CapSetIsDeterministic) {
  std::vector<Entry> ents = {mkEntry("bash", {"shell"}),
                             mkEntry("curl", {"shell", "net:fetch"}),
                             mkEntry("bash", {"shell"})};
  auto got = core::replay::capSet(ents);
  ASSERT_EQ(got.size(), 2u);
  EXPECT_EQ(got[0], "net:fetch");   // deterministic after sorting
  EXPECT_EQ(got[1], "shell");
}
