// Isolation boundary around yaml-cpp call sites.
// warden's main body compiles with -fno-exceptions, but yaml-cpp reports
// errors via exceptions. This header declares one free function whose
// implementation (yaml_bridge.cpp) compiles separately with -fexceptions and
// converts every yaml-cpp exception into a nullopt via try/catch. policy.cpp
// calls only this; it has no try/catch of its own.
#pragma once
#include <optional>
#include <string>

#include "core/policy/policy.h"

namespace core::policy {

// Parses one YAML document into a Policy. Syntactic corruption returns
// nullopt; unknown tags are silently ignored.
std::optional<Policy> parseOneYaml(const std::string& text);

}  // namespace core::policy
