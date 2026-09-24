// Trajectory jsonl persistence + per-tool privacy redaction.
// The privacy red line must be frozen in v1: changing it later breaks the
// comparability of existing journals.
#pragma once
#include <string>
#include <string_view>
#include <vector>

#include "core/journal/sha256.h"
#include "core/toolcall/toolcall.h"
#include "support/json.h"

namespace core::journal {

struct Entry {
  std::string t;          // ISO8601 UTC (passed in by the caller; journal never reads the clock)
  std::string ev;         // "tool" / "policy" / "turn"
  std::string tool;       // neutral name
  std::vector<std::string> scopes;
  std::vector<std::string> caps;
  support::Json input;    // already sanitized
  std::string decision;   // allow / ask / deny
  double durMs = 0;
};

// Per-tool redaction. Defined in privacy.cpp; takes the neutral IR.
// Red line: Write/Edit keeps only path + hash + byte count (content never
// enters the journal); Read keeps only the path; MCP keeps keys, hashes
// values; unknown tools are hashed whole; Bash is kept verbatim.
support::Json sanitizedInput(const core::ToolCall& tc);

// <root>/.warden/journal
std::string dir(std::string_view root);
// jsonl append (creates the directory; returns false on failure).
bool append(std::string_view root, std::string_view sessionId, const Entry& e);
// Read everything (for replay; a missing file returns empty and is not an
// error; bad lines are skipped).
std::vector<Entry> readAll(std::string_view root, std::string_view sessionId);

}  // namespace core::journal
