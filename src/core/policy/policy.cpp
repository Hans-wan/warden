// Loading and merging for the two-level policy. This file compiles with
// -fno-exceptions: all YAML parsing is delegated to yaml_bridge's
// parseOneYaml (its try/catch lives in an isolated unit).
#include "core/policy/policy.h"

#include <fstream>
#include <iterator>
#include <string>

#include "core/policy/default_yaml.h"  // CMake-generated: rawDefaultYaml
#include "core/policy/yaml_bridge.h"

namespace core::policy {

Policy defaultPolicy() {
  // The factory default is compile-time-embedded, controlled content; a parse
  // failure would be a programming error. It still goes through the same
  // path here, falling back to the structural default (Policy{Observe, {}}).
  auto p = parseOneYaml(rawDefaultYaml);
  if (p) return *p;
  return Policy{};
}

std::optional<Policy> load(const std::vector<std::string>& paths) {
  // Defaults first, then merge in order.
  Policy acc = defaultPolicy();
  for (const auto& path : paths) {
    std::ifstream in(path);
    if (!in) continue;  // missing/unreadable file: skip silently (zero-config guarantee)
    std::string text((std::istreambuf_iterator<char>(in)),
                     std::istreambuf_iterator<char>());
    auto p = parseOneYaml(text);
    if (!p) return std::nullopt;  // corrupt: fail as a whole
    acc.def = p->def;
    for (const auto& [tag, mode] : p->capModes) acc.capModes[tag] = mode;
  }
  return acc;
}

Mode decide(const Policy& p, cap::Tag t) {
  auto it = p.capModes.find(t);
  if (it != p.capModes.end()) return it->second;
  return p.def;
}

}  // namespace core::policy
