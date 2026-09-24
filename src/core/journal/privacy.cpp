// Per-tool privacy redaction — the privacy red line lives here.
// Every branch below is a contract: changing it later strips existing
// journals of their comparability.
#include <string>

#include "core/journal/journal.h"
#include "core/journal/sha256.h"

namespace core::journal {
namespace {

// String-field access; missing fields or type mismatches read as "".
std::string strField(const core::ToolCall& tc, std::string_view key) {
  const support::Json& v = tc.input.at(key);
  if (!v.is_string()) return "";
  return v.get<std::string>();
}

}  // namespace

support::Json sanitizedInput(const core::ToolCall& tc) {
  const std::string& name = tc.name;

  // Bash: keep the command verbatim (routing and replay both need the text).
  if (name == core::tc::Bash) {
    return support::Json::object{{"command", strField(tc, "command")}};
  }

  // Write: path + content hash + byte count only. Content never leaves.
  if (name == core::tc::Write) {
    const std::string content = strField(tc, "content");
    return support::Json::object{
        {"file_path", tc.filePath()},
        {"content_sha256", sha256::hex(content)},
        {"bytes", support::Json(static_cast<double>(content.size()))}};
  }

  // Edit: hash of old_string + "\0" + new_string joined; neither string ever leaves.
  if (name == core::tc::Edit) {
    const std::string old_s = strField(tc, "old_string");
    const std::string new_s = strField(tc, "new_string");
    std::string joined = old_s;
    joined.push_back('\0');
    joined += new_s;
    return support::Json::object{
        {"file_path", tc.filePath()},
        {"content_sha256", sha256::hex(joined)},
        {"bytes", support::Json(static_cast<double>(joined.size()))}};
  }

  // Read: path only; every other field is dropped (.env-style content must
  // not reach the journal).
  if (name == core::tc::Read) {
    return support::Json::object{{"file_path", tc.filePath()}};
  }

  // MCP: keys survive (they carry tool semantics), values are always hashed.
  if (tc.isMCP()) {
    support::Json::object out;
    for (const auto& [key, value] : tc.input.items()) {
      out[key] = support::Json(sha256::hex(value.dump()));
    }
    return support::Json(std::move(out));
  }

  // Unknown tools: hash the whole input (structure unknown, any field could be a secret).
  return support::Json::object{{"input_sha256", sha256::hex(tc.input.dump())}};
}

}  // namespace core::journal
