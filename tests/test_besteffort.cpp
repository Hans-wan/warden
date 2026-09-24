#include <gtest/gtest.h>
#include <algorithm>
#include <string>
#include "core/capability/besteffort.h"
#include "core/capability/tags.h"

using core::cap::Tag;
using namespace core::cap;

static std::vector<std::string> beTags(std::string_view cmd) {
  std::vector<std::string> out;
  for (Tag t : bestEffortExtra(cmd)) out.push_back(std::string(name(t)));
  std::sort(out.begin(), out.end());
  return out;
}

static void expectBE(std::string_view cmd, std::vector<std::string> want) {
  std::sort(want.begin(), want.end());
  EXPECT_EQ(beTags(cmd), want) << cmd;
}

TEST(BestEffort, PkgInstall) {
  expectBE("npm install left-pad", {"pkg:install"});
  expectBE("npm i -D typescript", {"pkg:install"});
  expectBE("pip install requests", {"pkg:install"});
  expectBE("cargo add serde", {"pkg:install"});
  expectBE("brew install jq", {"pkg:install"});
}

TEST(BestEffort, EnvRead) {
  expectBE("env", {"env:read"});
  expectBE("printenv HOME", {"env:read"});
}

TEST(BestEffort, SysModify) {
  expectBE("chmod 755 run.sh", {"sys:modify"});
  expectBE("sudo apt update", {"sys:modify"});
}

TEST(BestEffort, ProcKill) {
  expectBE("pkill -f server", {"proc:kill"});
}

TEST(BestEffort, Container) {
  expectBE("docker ps", {"container"});
  expectBE("kubectl get pods", {"container"});
}

TEST(BestEffort, DBWrite) {
  expectBE(R"x(psql -c "insert into t values(1)")x", {"db:write"});
  expectBE(R"x(mysql -e "update t set x=1")x", {"db:write"});
}

TEST(BestEffort, CloudWrite) {
  expectBE("aws s3 rm s3://b/k", {"cloud:write"});
  expectBE("gcloud storage cp a gs://b", {"cloud:write"});
}

TEST(BestEffort, CoreTagsNotDuplicated) {
  // curl is already covered by the core set's net:fetch; it must not reappear here.
  EXPECT_TRUE(beTags("curl https://x").empty());
}

TEST(BestEffort, EmptyAndInnocuousYieldNothing) {
  EXPECT_TRUE(beTags("").empty());
  EXPECT_TRUE(beTags("ls -la").empty());
}
