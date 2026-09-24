#include "core/capability/capability.h"

#include <algorithm>
#include <cctype>
#include <string>

#include "core/capability/bash.h"
#include "core/capability/besteffort.h"

namespace core::cap {

namespace {
std::string toLower(std::string_view s) {
  std::string out(s);
  std::transform(out.begin(), out.end(), out.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return out;
}
}  // namespace

bool isSecretPath(std::string_view path) {
  const std::string p = toLower(path);
  for (std::string_view pat : {".env", "id_rsa", "id_ed25519", ".aws/credentials",
                               ".ssh/", ".netrc", ".npmrc", ".docker/config.json"}) {
    if (p.find(pat) != std::string::npos) return true;
  }
  return false;
}

std::vector<Tag> of(const ToolCall& tc) {
  if (tc.isMCP()) return {Tag::MCPCall};

  switch (tc.kind) {
    case ToolKind::kRead: {
      std::vector<Tag> tags{Tag::FsRead};
      if (isSecretPath(tc.filePath())) tags.push_back(Tag::SecretsRead);
      return tags;
    }
    case ToolKind::kWrite:
    case ToolKind::kEdit: {
      std::vector<Tag> tags{Tag::FsWrite};
      if (isSecretPath(tc.filePath())) tags.push_back(Tag::SecretsRead);
      return tags;
    }
    case ToolKind::kTask:
      return {Tag::AgentSpawn};
    case ToolKind::kSkill:
      return {};  // attribution lives elsewhere; a skill call adds no capability of its own
    case ToolKind::kBash: {
      const std::string cmd = tc.str("command");
      // Union of the core set (fromBash) and the best-effort set
      // (bestEffortExtra). bestEffortExtra's contract is "only tags the core
      // set doesn't cover", so duplicates are impossible in theory; dedup
      // anyway, in the same style as fromBash's add lambda.
      std::vector<Tag> tags = fromBash(cmd);
      auto add = [&](Tag t) {
        if (std::find(tags.begin(), tags.end(), t) == tags.end()) tags.push_back(t);
      };
      for (Tag t : bestEffortExtra(cmd)) add(t);
      return tags;
    }
    case ToolKind::kUnknown:
      return {};
  }
  return {};
}

}  // namespace core::cap
