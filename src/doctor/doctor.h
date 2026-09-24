// doctor self-check: a security tool that fails silently is worse
// than none.
#pragma once
#include <string>
#include <vector>

namespace core::doctor {

struct Check {
  std::string name;
  bool ok = false;
  std::string note;
};

// Runs every check. Any failure means the gate may be silently down.
std::vector<Check> run(const std::string& root, const std::string& binPath);

// Locates the plugin root by walking up from the binary path looking for
// hooks/hooks.json. Returns "" when the layout is absent (source-tree runs).
std::string findPluginRootPublic(const std::string& binPath);

}  // namespace core::doctor
