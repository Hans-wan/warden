#include <gtest/gtest.h>

#include <string>

#include "adapters/claude-code/hooks.h"
#include "adapters/claude-code/output.h"
#include "hooks_cmd.h"

TEST(HooksGen, ContainsFourEventsWithAsyncSplit) {
  // The plugin contract now ships bin/warden-launcher (the platform picker),
  // so the template argument tracks that form; this test only pins the
  // events + async split, not the command path shape.
  auto raw = cc::hooksJson("${CLAUDE_PLUGIN_ROOT}/bin/warden-launcher");
  EXPECT_NE(raw.find("\"SessionStart\""), std::string::npos);
  EXPECT_NE(raw.find("\"PreToolUse\""), std::string::npos);
  EXPECT_NE(raw.find("\"PostToolUse\""), std::string::npos);
  EXPECT_NE(raw.find("\"Stop\""), std::string::npos);
  EXPECT_NE(raw.find("\"async\": true"), std::string::npos)
      << "post-tool/stop must be async: true (they cannot block)";
  EXPECT_NE(raw.find("pre-tool"), std::string::npos);
}

// The shipped plugin dispatches every hook through the platform launcher:
// the generated JSON must name warden-launcher, not the raw binary.
TEST(HooksGen, GeneratedJsonUsesLauncher) {
  auto out = cc::hooksJson("${CLAUDE_PLUGIN_ROOT}/bin/warden-launcher");
  EXPECT_NE(out.find("warden-launcher pre-tool"), std::string::npos);
  EXPECT_NE(out.find("warden-launcher session-start"), std::string::npos);
}

TEST(HooksGen, NoSessionEndDeclared) {
  // SessionEnd has a 1.5 s budget and is forbidden as a persistence path.
  EXPECT_EQ(cc::hooksJson("bin").find("SessionEnd"), std::string::npos);
}

// Extra case: post-tool/stop must be async; pre-tool must never be (it has to block).
TEST(HooksGen, PreToolIsNotAsync) {
  const auto raw = cc::hooksJson("bin pre");
  const std::size_t begin = raw.find("\"PreToolUse\"");
  ASSERT_NE(begin, std::string::npos);
  const std::size_t end = raw.find("\"PostToolUse\"");
  ASSERT_NE(end, std::string::npos);
  ASSERT_LT(begin, end);
  // Look only at the PreToolUse block (a whole-text async scan would count
  // PostToolUse's async).
  const std::string block = raw.substr(begin, end - begin);
  EXPECT_NE(block.find("pre-tool"), std::string::npos);
  EXPECT_EQ(block.find("async"), std::string::npos)
      << "pre-tool must be synchronous: async hooks cannot block";
}

// Extra case: skills-section parsing.
TEST(HooksGen, SkillDeclsInlineAndBlockList) {
  const char* yaml =
      "skills:\n"
      "  pdf-tools:\n"
      "    capabilities: [fs:read, shell]\n"
      "    persist: session\n"
      "  data-crunch:\n"
      "    capabilities:\n"
      "      - fs:read\n"
      "  bare:\n";
  auto decls = warden::parseSkillDecls(yaml);
  ASSERT_EQ(decls.size(), 3u);
  EXPECT_EQ(decls[0].first, "pdf-tools");
  ASSERT_EQ(decls[0].second.caps.size(), 2u);
  EXPECT_EQ(decls[0].second.caps[0], "fs:read");
  EXPECT_EQ(decls[0].second.caps[1], "shell");
  EXPECT_TRUE(decls[0].second.persist);
  EXPECT_EQ(decls[1].first, "data-crunch");
  ASSERT_EQ(decls[1].second.caps.size(), 1u);
  EXPECT_EQ(decls[1].second.caps[0], "fs:read");
  EXPECT_FALSE(decls[1].second.persist);
  // Name only, no properties: an empty declaration (observe semantics).
  EXPECT_EQ(decls[2].first, "bare");
  EXPECT_TRUE(decls[2].second.caps.empty());
  EXPECT_FALSE(decls[2].second.persist);
}

// Extra case: top-level keys outside the skills section must not be misread as
// skills; comments are ignored.
TEST(HooksGen, SkillDeclsIgnoreOtherTopLevelKeys) {
  const char* yaml =
      "default: observe\n"
      "capabilities:\n"
      "  fs:write: enforce\n"
      "skills:\n"
      "  # comment line\n"
      "  pdf-tools:\n"
      "    capabilities: [fs:read]  # trailing comment\n";
  auto decls = warden::parseSkillDecls(yaml);
  ASSERT_EQ(decls.size(), 1u);
  EXPECT_EQ(decls[0].first, "pdf-tools");
  ASSERT_EQ(decls[0].second.caps.size(), 1u);
  EXPECT_EQ(decls[0].second.caps[0], "fs:read");
}

// ---- Humane denial messages ------------------------------------------------
// The two-argument deny/ask overloads append a plain-language companion
// sentence to the same permissionDecisionReason. The machine-readable line
// must stay first and byte-identical: CI consumers parse it as a prefix.

TEST(HumaneDenial, MachineLineStaysByteIdentical) {
  auto out = cc::deny("capability fs:write not authorized by any open scope; responsible: [pdf-tools]");
  // The machine line must remain a prefix, byte-identical (CI parses it).
  EXPECT_NE(out.find("capability fs:write not authorized by any open scope; responsible: [pdf-tools]"),
            std::string::npos);
}

TEST(HumaneDenial, HintAppendedInSameReason) {
  auto out = cc::deny("capability fs:write not authorized by any open scope",
                      "warden blocked this call: the active skill only declared read access. "
                      "Run /warden:status for the full picture, or add fs:write to the skill in "
                      ".warden/policy.yaml if this is expected.");
  // Both parts live inside permissionDecisionReason, machine line first.
  const auto pos = out.find("capability fs:write not authorized by any open scope");
  const auto hint = out.find("/warden:status");
  EXPECT_NE(pos, std::string::npos);
  EXPECT_NE(hint, std::string::npos);
  EXPECT_GT(hint, pos);
}

TEST(HumaneDenial, AskHasReviewHint) {
  auto out = cc::ask("capability net:fetch not authorized by any open scope",
                     "warden flagged this call for review — approve if it looks right. "
                     "Run /warden:status for details.");
  EXPECT_NE(out.find("flagged this call for review"), std::string::npos);
}
