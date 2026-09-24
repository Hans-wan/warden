// Neutral tool-call IR. No host-specific concept may appear in this header —
// literals such as host tool names and host payload field names are not part
// of core's vocabulary (neutral constants are). Mapping from host raw names to
// neutral names happens in the adapters layer.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

#include "support/json.h"  // bundled minimal JSON (see src/support/json.h)

namespace core::tc {
// Neutral tool names. Host raw names map onto these in the adapters layer.
inline constexpr const char* Bash = "bash";
inline constexpr const char* Read = "read";
inline constexpr const char* Write = "write";
inline constexpr const char* Edit = "edit";
inline constexpr const char* Task = "task";
inline constexpr const char* Skill = "skill";
inline constexpr const char* MCPPrefix = "mcp:";
}  // namespace core::tc

namespace core {

// Neutral kinds for neutral tool names. The structured basis for capability
// judgment; host-specific kinds all collapse to kUnknown. kind is derived from
// the neutral name; MCP prefixes and unknown names yield kUnknown.
enum class ToolKind : std::uint8_t {
  kUnknown = 0,
  kRead,
  kWrite,
  kEdit,
  kTask,
  kSkill,
  kBash,
};

// Neutral name -> neutral kind. Unknown names (incl. MCP prefixes) return kUnknown.
ToolKind kindOfName(std::string_view name);

struct ToolCall {
  std::string name;
  Json input;
  ToolKind kind = ToolKind::kUnknown;

  // Constructs and derives kind from the neutral name.
  static ToolCall make(std::string name, Json input);

  bool isMCP() const;
  // Safe string-field access; missing fields or type mismatches return "".
  std::string str(std::string_view key) const;
  // Convenience accessor for file_path; missing/non-string returns "".
  std::string filePath() const;
};

}  // namespace core
