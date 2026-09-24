#include <gtest/gtest.h>

#include <string>

#include "adapters/claude-code/output.h"
#include "adapters/claude-code/payload.h"

using namespace cc;

static const char kSample[] = R"({
  "session_id": "abc123",
  "cwd": "/proj",
  "permission_mode": "default",
  "hook_event_name": "PreToolUse",
  "tool_name": "Bash",
  "tool_use_id": "toolu_01",
  "tool_input": {"command": "rm -rf /tmp/x", "description": "clean"}
})";

TEST(Payload, ParseSample) {
  std::string err;
  auto p = parsePayload(kSample, err);
  ASSERT_TRUE(p.has_value()) << err;
  EXPECT_EQ(p->sessionId, "abc123");
  EXPECT_EQ(p->cwd, "/proj");
  EXPECT_EQ(p->permissionMode, "default");
  EXPECT_EQ(p->hookEventName, "PreToolUse");
  EXPECT_EQ(p->toolName, "Bash");
  EXPECT_EQ(p->toolUseId, "toolu_01");
  EXPECT_EQ(p->agentId, "");
  EXPECT_EQ(p->toolInput.get_string("command"), "rm -rf /tmp/x")
      << "the command string must pass through verbatim";
  EXPECT_EQ(p->toolInput.get_string("description"), "clean");
}

TEST(Payload, ParseSubagentFields) {
  // Tool calls inside a subagent carry top-level agent_id/agent_type (measured fact 3c).
  static const char kSub[] = R"({
    "session_id": "s1",
    "hook_event_name": "PreToolUse",
    "agent_id": "a7678f2d0daf76ac5",
    "agent_type": "general-purpose",
    "tool_name": "Bash",
    "tool_input": {"command": "echo SUBAGENT_ECHO_OK"}
  })";
  std::string err;
  auto p = parsePayload(kSub, err);
  ASSERT_TRUE(p.has_value()) << err;
  EXPECT_EQ(p->agentId, "a7678f2d0daf76ac5");
  EXPECT_EQ(p->agentType, "general-purpose");
}

TEST(Payload, MissingAgentFieldsAreEmptyNotError) {
  // Main-agent calls lack both keys: absence is normal, not an error.
  std::string err;
  auto p = parsePayload(kSample, err);
  ASSERT_TRUE(p.has_value()) << err;
  EXPECT_EQ(p->agentId, "");
  EXPECT_EQ(p->agentType, "");
}

TEST(Payload, ToNeutralIsTheOnlyMappingPoint) {
  struct Case {
    const char* raw;
    const char* want;
  };
  Case cases[] = {
      {"Bash", "bash"},
      {"Read", "read"},
      {"Write", "write"},
      {"Edit", "edit"},
      {"Task", "task"},
      // Measured: Claude Code actually sends "Agent";
      // the neutral name stays task.
      {"Agent", "task"},
      {"Skill", "skill"},
      {"mcp__github__create_issue", "mcp:github:create_issue"},
  };
  for (auto& c : cases) {
    Payload p;
    p.toolName = c.raw;
    EXPECT_EQ(toNeutral(p).name, c.want) << c.raw;
  }
}

TEST(Payload, ToNeutralPreservesInput) {
  Payload p;
  p.toolName = "Bash";
  p.toolInput = support::Json::object{{"command", "ls -la"}};
  core::ToolCall tc = toNeutral(p);
  EXPECT_EQ(tc.name, "bash");
  EXPECT_EQ(tc.str("command"), "ls -la");
  EXPECT_EQ(tc.kind, core::ToolKind::kBash);
}

TEST(Payload, ToNeutralUnknownName) {
  Payload p;
  p.toolName = "SomethingWeird";
  EXPECT_EQ(toNeutral(p).name, "SomethingWeird");
}

TEST(Output, DenyShape) {
  auto s = deny("fs:write not authorized");
  EXPECT_NE(s.find("\"permissionDecision\":\"deny\""), std::string::npos);
  EXPECT_NE(s.find("\"hookEventName\":\"PreToolUse\""), std::string::npos);
  EXPECT_NE(s.find("fs:write"), std::string::npos);
}

TEST(Output, AskShape) {
  EXPECT_NE(ask("allow git push?").find("\"permissionDecision\":\"ask\""),
            std::string::npos);
}

TEST(Output, AllowShape) {
  EXPECT_NE(allow("").find("\"permissionDecision\":\"allow\""),
            std::string::npos);
}

TEST(Output, EmptyReasonOmitsField) {
  auto s = allow("");
  EXPECT_EQ(s.find("permissionDecisionReason"), std::string::npos)
      << "an empty reason must omit the field";
}

TEST(Output, NonEmptyReasonKept) {
  auto s = deny("blocked!");
  EXPECT_NE(s.find("\"permissionDecisionReason\":\"blocked!\""),
            std::string::npos);
}

TEST(Payload, ParseGarbageReturnsNullopt) {
  std::string err;
  EXPECT_FALSE(parsePayload("not json", err).has_value());
  EXPECT_FALSE(err.empty());
}
