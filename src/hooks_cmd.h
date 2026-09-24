// Implementations of the four hook subcommands (session-start / pre-tool / post-tool / stop).
// Every hook process starts fresh: state must be persisted, adjudication must
// be synchronous, recording should be async. Every warden-internal failure
// takes the non-blocking path (exit 1) — the user's session is never wrecked.
#pragma once

#include <string>
#include <utility>
#include <vector>

namespace warden {

// All four read the host payload from stdin and exit per their own contracts.
int sessionStart();  // SessionStart: create state, ensure journal dir, write .gitignore
int preTool();       // PreToolUse: the only sync hook; may block (allow/ask/deny)
int postTool();      // PostToolUse: records a tool-result summary (async hook)
int stop();          // Stop: turn boundary; demotes turn-tier caps (async hook)

// A `skills:` section entry from policy.yaml (v1 has no registry; the project
// policy file is the only declaration source). An undeclared skill gets empty
// caps + persist=false (observe semantics).
struct SkillDecl {
  std::vector<std::string> caps;
  bool persist = false;
};

// Reads the skills section of <cwd>/.warden/policy.yaml. A missing file or
// section yields an empty table. Hand-written mini-parser: warden's main body
// compiles -fno-exceptions and yaml-cpp is only allowed in yaml_bridge.cpp
// (not on this file's whitelist), so yaml-cpp is out of reach here.
std::string skillDeclPath(const std::string& cwd);
std::vector<std::pair<std::string, SkillDecl>> parseSkillDecls(
    const std::string& yamlText);

}  // namespace warden
