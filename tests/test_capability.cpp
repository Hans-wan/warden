#include <gtest/gtest.h>

#include <algorithm>
#include <set>
#include <vector>

#include "core/capability/capability.h"
#include "core/toolcall/toolcall.h"

using core::ToolCall;
using core::cap::Tag;
using core::tc::Bash;
using core::tc::Edit;
using core::tc::Read;
using core::tc::Skill;
using core::tc::Task;
using core::tc::Write;
using namespace core::cap;

static void expectOf(const ToolCall& tc, std::vector<Tag> want) {
  std::sort(want.begin(), want.end());
  EXPECT_EQ(of(tc), want) << "tool: " << tc.name;
}

TEST(Capability, ReadYieldsFsRead) {
  expectOf(ToolCall::make(Read, Json::object{{"file_path", "/a.txt"}}), {Tag::FsRead});
}

TEST(Capability, WriteYieldsFsWrite) {
  expectOf(ToolCall::make(Write, Json::object{{"file_path", "/a.txt"}, {"content", "x"}}),
      {Tag::FsWrite});
}

TEST(Capability, EditYieldsFsWrite) {
  expectOf(ToolCall::make(Edit, Json::object{{"file_path", "/a.txt"}}), {Tag::FsWrite});
}

TEST(Capability, TaskYieldsAgentSpawn) {
  expectOf(ToolCall::make(Task, Json::object()), {Tag::AgentSpawn});
}

TEST(Capability, SecretPathAddsSecretsRead) {
  auto r = of(ToolCall::make(Read, Json::object{{"file_path", "/repo/.env"}}));
  EXPECT_NE(std::find(r.begin(), r.end(), Tag::SecretsRead), r.end());
  auto r2 = of(ToolCall::make(Read, Json::object{{"file_path", "/home/u/.ssh/id_rsa"}}));
  EXPECT_NE(std::find(r2.begin(), r2.end(), Tag::SecretsRead), r2.end());
}

TEST(Capability, WriteToSecretPathYieldsWritePlusSecrets) {
  auto r = of(ToolCall::make(Write, Json::object{{"file_path", "/x/.aws/credentials"}}));
  std::set<Tag> got(r.begin(), r.end());
  EXPECT_EQ(got, (std::set<Tag>{Tag::FsWrite, Tag::SecretsRead}));
}

TEST(Capability, BashDelegatesToFromBash) {
  auto r = of(ToolCall::make(Bash, Json::object{{"command", "git push origin"}}));
  std::set<Tag> got(r.begin(), r.end());
  EXPECT_EQ(got, (std::set<Tag>{Tag::Shell, Tag::GitPush}));
}

TEST(Capability, MCPYieldsMCPCall) {
  expectOf(ToolCall::make("mcp:github:create_issue", Json::object{{"title", "x"}}), {Tag::MCPCall});
}

TEST(Capability, SkillYieldsNothing) {
  // A Skill call only changes scopes, adding no capability — attribution is attribution's job.
  EXPECT_TRUE(of(ToolCall::make(Skill, Json::object())).empty());
}

TEST(Capability, UnknownToolYieldsNothing) {
  EXPECT_TRUE(of(ToolCall::make("mystery", Json::object())).empty());
}
