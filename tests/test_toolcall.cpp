#include <gtest/gtest.h>

#include "core/toolcall/toolcall.h"

using core::ToolCall;
using namespace core::tc;

TEST(ToolCall, IsMCP) {
  EXPECT_TRUE(ToolCall::make("mcp:github:create_issue", Json::object()).isMCP());
  EXPECT_FALSE(ToolCall::make("bash", Json::object()).isMCP());
  EXPECT_FALSE(ToolCall::make("read", Json::object()).isMCP());
}

TEST(ToolCall, StrMissingKeyReturnsEmpty) {
  auto tc = ToolCall::make("write", Json::object{{"file_path", "/a/b.txt"}});
  EXPECT_EQ(tc.str("file_path"), "/a/b.txt");
  EXPECT_EQ(tc.str("nonexistent"), "");
}

TEST(ToolCall, StrNonStringValueReturnsEmpty) {
  Json input = Json::object{{"timeout", Json(120000)}};
  EXPECT_EQ(ToolCall::make("bash", input).str("timeout"), "");
}
