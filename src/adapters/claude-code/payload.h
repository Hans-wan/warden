// Claude Code adapter: payload parsing and name mapping.
// Claude Code-specific concepts may appear only in adapters/claude-code/;
// core/ must not include this header — adding a new agent in v2 means adding
// an adapter, never modifying existing ones.
#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "core/toolcall/toolcall.h"
#include "support/json.h"

namespace cc {

struct Payload {
  std::string sessionId;
  std::string cwd;
  std::string permissionMode;
  std::string hookEventName;
  std::string toolName;
  std::string toolUseId;
  support::Json toolInput;  // kept verbatim (non-string fields included)
  std::string agentId;
  std::string agentType;
};

// Parses the stdin payload; on failure sets err and returns nullopt.
std::optional<Payload> parsePayload(std::string_view json, std::string& err);

// Raw tool name -> neutral name. The single mapping point.
core::ToolCall toNeutral(const Payload& p);

}  // namespace cc
