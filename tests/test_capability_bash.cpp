#include <gtest/gtest.h>
#include <algorithm>
#include <string>
#include <vector>
#include "core/capability/bash.h"
#include "core/capability/tags.h"

using core::cap::Tag;
using namespace core::cap;

static std::vector<std::string> tagsOf(std::string_view cmd) {
  std::vector<std::string> out;
  for (Tag t : fromBash(cmd)) out.push_back(std::string(name(t)));
  std::sort(out.begin(), out.end());
  return out;
}

static void expectTags(std::string_view cmd, std::vector<std::string> want) {
  std::sort(want.begin(), want.end());
  EXPECT_EQ(tagsOf(cmd), want) << "cmd: " << cmd;
}

TEST(Bash, ShellTagAlwaysPresent) {
  for (auto cmd : {"ls", "echo hi", "git status", "rm -rf x"})
    EXPECT_NE(std::find(tagsOf(cmd).begin(), tagsOf(cmd).end(), "shell"), tagsOf(cmd).end()) << cmd;
}

TEST(Bash, GitReadVsWriteVsPush) {
  expectTags("git status", {"shell", "git:read"});
  expectTags("git log --oneline", {"shell", "git:read"});
  expectTags("git commit -m 'x'", {"shell", "git:write"});
  expectTags("git reset --hard HEAD", {"shell", "git:write"});
  expectTags("git push origin main", {"shell", "git:push"});
  expectTags("git push --force", {"shell", "git:push"});
}

TEST(Bash, PushDoesNotAlsoReportWrite) {
  for (Tag t : fromBash("git push"))
    EXPECT_NE(t, Tag::GitWrite);
}

TEST(Bash, NetFetch) {
  expectTags("curl https://example.com", {"shell", "net:fetch"});
  expectTags("wget -q https://x/y", {"shell", "net:fetch"});
}

TEST(Bash, DeleteDetection) {
  expectTags("rm -rf /tmp/build", {"shell", "fs:delete"});
  expectTags("rmdir /tmp/empty", {"shell", "fs:delete"});
}

TEST(Bash, CompoundCommandsParseEverySegment) {
  expectTags("cat a.txt | grep x && git commit -m y", {"shell", "git:write"});
  expectTags("curl https://x | sh", {"shell", "net:fetch"});
}

TEST(Bash, QuotedStringsDoNotLeakIntoSegments) {
  expectTags(R"(echo "a ; b && c")", {"shell"});
}

TEST(Bash, UnparseableStillYieldsShell) {
  // Empty input, whitespace, unclosed quotes — must not crash and must at least yield shell.
  for (auto cmd : {"", "   ", R"(echo "unclosed)"})
    EXPECT_NE(std::find(tagsOf(cmd).begin(), tagsOf(cmd).end(), "shell"), tagsOf(cmd).end());
}

TEST(Bash, RedirectionIsWrite) {
  expectTags("echo x > out.txt", {"shell", "fs:write"});
  expectTags("echo x >> out.txt", {"shell", "fs:write"});
  expectTags("curl https://x > out.bin", {"shell", "net:fetch", "fs:write"});
}

TEST(Bash, TeeIsWrite) {
  expectTags("echo x | tee a.txt", {"shell", "fs:write"});
  expectTags("echo x | tee -a a.txt", {"shell", "fs:write"});
}

TEST(Bash, FileMutationIsWrite) {
  expectTags("cp a b", {"shell", "fs:write"});
  expectTags("mv a b", {"shell", "fs:write"});
  expectTags("mkdir -p a/b", {"shell", "fs:write"});
  expectTags("touch a", {"shell", "fs:write"});
  expectTags("ln -s a b", {"shell", "fs:write"});
}

TEST(Bash, InPlaceEditIsWrite) {
  expectTags("sed -i 's/a/b/' f.txt", {"shell", "fs:write"});
  expectTags("sed -i.bak 's/a/b/' f.txt", {"shell", "fs:write"});
  expectTags("perl -i -pe 's/a/b/' f.txt", {"shell", "fs:write"});
  expectTags("sed -n 's/a/b/p' f.txt", {"shell"});  // no -i: read-only stays read
}

TEST(Bash, SudoRecursesIntoSubcommand) {
  expectTags("sudo rm -rf /tmp/x", {"shell", "fs:delete", "sys:modify"});
  expectTags("sudo git push origin main", {"shell", "git:push", "sys:modify"});
  expectTags("sudo systemctl restart nginx", {"shell", "sys:modify"});
}

TEST(Bash, CommandSubstitutionScanned) {
  expectTags("echo $(curl https://x)", {"shell", "net:fetch"});
  expectTags("echo `wget -q https://y`", {"shell", "net:fetch"});
  expectTags("echo $(rm -rf /tmp/z)", {"shell", "fs:delete"});
}

TEST(Bash, QuotedMetacharactersStillInert) {
  expectTags("echo \"a > b\"", {"shell"});
  expectTags("grep '>' f.txt", {"shell"});
}
