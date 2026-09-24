// This translation unit compiles separately with -fexceptions (see the
// set_source_files_properties in CMakeLists.txt). It is the only place in
// warden that touches yaml-cpp, and the only place allowed to use try/catch.
// Its job: keep yaml-cpp's exception-based world out of warden's
// -fno-exceptions world, converting exceptions into std::optional failures
// at the boundary.
#include "core/policy/yaml_bridge.h"

#include <yaml-cpp/yaml.h>

namespace core::policy {

std::optional<Policy> parseOneYaml(const std::string& text) {
  Policy p;
  try {
    auto node = YAML::Load(text);
    if (node["default"]) {
      auto s = node["default"].as<std::string>();
      if (s == "observe") p.def = Mode::Observe;
      else if (s == "ask") p.def = Mode::Ask;
      else if (s == "enforce") p.def = Mode::Enforce;
    }
    if (node["capabilities"]) {
      for (auto it : node["capabilities"]) {
        auto tag = cap::fromName(it.first.as<std::string>());
        if (!tag) continue;  // unknown tag: ignore (tolerant)
        auto m = it.second.as<std::string>();
        if (m == "observe") p.capModes[*tag] = Mode::Observe;
        else if (m == "ask") p.capModes[*tag] = Mode::Ask;
        else if (m == "enforce") p.capModes[*tag] = Mode::Enforce;
      }
    }
  } catch (...) {
    // yaml-cpp reports errors via exceptions (syntax, type, ...); the
    // boundary converts them all to failure.
    return std::nullopt;
  }
  return p;
}

}  // namespace core::policy
