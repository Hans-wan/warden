// Blocking-output contract: exit 0 + stdout JSON.
#pragma once

#include <string>

namespace cc {

std::string allow(const std::string& reason);
std::string ask(const std::string& reason);
std::string deny(const std::string& reason);
// Two-argument forms: the second argument is a plain-language companion
// sentence appended to permissionDecisionReason after a space. The reason
// line stays first and byte-identical, so CI consumers that parse it as a
// prefix keep working.
std::string ask(const std::string& reason, const std::string& hint);
std::string deny(const std::string& reason, const std::string& hint);

// Prints and exits 0 (allow/ask/deny all go through this). [[noreturn]]:
// call sites no longer lean on exit's side effects; nothing runs after emit
// (making the contract explicit — see hooks_cmd preTool).
[[noreturn]] void emit(const std::string& json);
// warden's own failure: stderr + exit 1. Exit 1 does not block; the tool runs
// anyway — better to miss a block than to wreck the user's session.
[[noreturn]] void failNonBlocking(const std::string& msg);

}  // namespace cc
