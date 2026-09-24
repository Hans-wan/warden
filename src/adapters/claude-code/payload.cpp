// Claude Code payload parsing and raw-name -> neutral-name mapping.
// This file is the only landing spot for host-specific field/tool names
// (core/ and support/ must never contain them).
#include "adapters/claude-code/payload.h"

#include <string>

namespace cc {
namespace {

// Object key read: missing keys or type mismatches read as "" (see findings
// facts 1/2: keys are optional; absent means the field does not exist).
std::string strField(const Json& j, const char* key) {
  return j.get_string(key);
}

}  // namespace

std::optional<Payload> parsePayload(std::string_view json, std::string& err) {
  Json j = Json::parse(json, err);
  if (!err.empty()) return std::nullopt;
  if (!j.is_object()) {
    err = "payload is not a JSON object";
    return std::nullopt;
  }

  Payload p;
  p.sessionId = strField(j, "session_id");
  p.cwd = strField(j, "cwd");
  p.permissionMode = strField(j, "permission_mode");
  p.hookEventName = strField(j, "hook_event_name");
  p.toolName = strField(j, "tool_name");
  p.toolUseId = strField(j, "tool_use_id");
  // tool_input is kept verbatim (non-string fields included, e.g. the
  // run_in_background boolean); when missing it stays null and downstream
  // treats that as "no input".
  {
    auto it = j.find("tool_input");
    if (it != j.end()) p.toolInput = it->second;
  }
  // Every tool call inside a subagent carries top-level agent_id/agent_type
  // (not part of the documented payload shape); the main agent does not —
  // absence is normal, and the empty string means "main agent".
  p.agentId = strField(j, "agent_id");
  p.agentType = strField(j, "agent_type");
  return p;
}

core::ToolCall toNeutral(const Payload& p) {
  const std::string& raw = p.toolName;

  // MCP: the host writes mcp__<server>__<tool>; the neutral form is
  // mcp:<server>:<tool>.
  constexpr std::string_view kMCPRaw = "mcp__";
  if (raw.rfind(kMCPRaw, 0) == 0) {
    std::string rest = raw.substr(kMCPRaw.size());
    std::string out;
    out.reserve(rest.size() + std::string_view(core::tc::MCPPrefix).size());
    out += core::tc::MCPPrefix;
    for (std::size_t i = 0; i < rest.size(); ++i) {
      if (rest[i] == '_' && i + 1 < rest.size() && rest[i + 1] == '_') {
        out.push_back(':');
        ++i;  // consume the second underscore
      } else {
        out.push_back(rest[i]);
      }
    }
    return core::ToolCall::make(std::move(out), p.toolInput);
  }

  // Raw name -> neutral name, checked one by one against measured host tool
  // names. Note: the dispatch tool measured as "Agent" (findings conclusion
  // 8), not "Task"; both raw names map to the neutral task name so different
  // host versions keep working.
  std::string name = raw;
  if (raw == "Bash") {
    name = core::tc::Bash;
  } else if (raw == "Read") {
    name = core::tc::Read;
  } else if (raw == "Write") {
    name = core::tc::Write;
  } else if (raw == "Edit") {
    name = core::tc::Edit;
  } else if (raw == "Task" || raw == "Agent") {
    name = core::tc::Task;
  } else if (raw == "Skill") {
    name = core::tc::Skill;
  }
  // Unknown names pass through as-is (no information lost; make derives kUnknown).
  return core::ToolCall::make(std::move(name), p.toolInput);
}

}  // namespace cc
