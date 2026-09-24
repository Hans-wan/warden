#include "core/toolcall/toolcall.h"

namespace core {

ToolKind kindOfName(std::string_view name) {
  if (name == tc::Read) return ToolKind::kRead;
  if (name == tc::Write) return ToolKind::kWrite;
  if (name == tc::Edit) return ToolKind::kEdit;
  if (name == tc::Task) return ToolKind::kTask;
  if (name == tc::Skill) return ToolKind::kSkill;
  if (name == tc::Bash) return ToolKind::kBash;
  return ToolKind::kUnknown;
}

ToolCall ToolCall::make(std::string name, Json input) {
  ToolKind kind = kindOfName(name);
  return ToolCall{std::move(name), std::move(input), kind};
}

bool ToolCall::isMCP() const {
  return name.rfind(tc::MCPPrefix, 0) == 0;
}

std::string ToolCall::str(std::string_view key) const {
  if (!input.is_object()) return "";
  const Json& v = input.at(key);
  if (!v.is_string()) return "";
  return v.get<std::string>();
}

std::string ToolCall::filePath() const { return str("file_path"); }

}  // namespace core
