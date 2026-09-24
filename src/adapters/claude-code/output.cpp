// Blocking-output assembly (exit 0 + stdout JSON).
// Built with support::Json and dumped, never hand-concatenated — escaping
// stays safe when reason contains quotes or newlines.
#include "adapters/claude-code/output.h"

#include <cstdlib>
#include <iostream>

#include "support/json.h"

namespace cc {
namespace {

constexpr const char* kPreToolUse = "PreToolUse";

// Assembles hookSpecificOutput. An empty reason omits permissionDecisionReason
// (Json objects can't have "conditional fields", so build the base object
// first and assign conditionally). The humane hint is folded into the same
// permissionDecisionReason after a single space, so the host surfaces one
// combined reason and the machine-readable line stays a byte-identical prefix
// (CI readers keep parsing it).
std::string decision(const char* d, const std::string& reason,
                     const std::string& hint) {
  std::string full = reason;
  if (!hint.empty()) {
    if (!full.empty()) full += " ";
    full += hint;
  }
  Json hso = Json::object{
      {"hookEventName", Json(kPreToolUse)},
      {"permissionDecision", Json(d)},
  };
  if (!full.empty()) {
    hso["permissionDecisionReason"] = Json(full);
  }
  Json root = Json::object{{"hookSpecificOutput", std::move(hso)}};
  return root.dump();
}

}  // namespace

std::string allow(const std::string& reason) { return decision("allow", reason, ""); }
std::string ask(const std::string& reason) { return decision("ask", reason, ""); }
std::string deny(const std::string& reason) { return decision("deny", reason, ""); }
std::string ask(const std::string& reason, const std::string& hint) {
  return decision("ask", reason, hint);
}
std::string deny(const std::string& reason, const std::string& hint) {
  return decision("deny", reason, hint);
}

[[noreturn]] void emit(const std::string& json) {
  std::cout << json << std::endl;
  std::exit(0);
}

[[noreturn]] void failNonBlocking(const std::string& msg) {
  std::cerr << msg << std::endl;
  std::exit(1);
}

}  // namespace cc
