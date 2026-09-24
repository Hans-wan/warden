// Session state: the single persistence point that survives across hook processes.
// Each hook process starts fresh, so state must hit the disk; single-file
// read-write-all — with so few scopes this beats any incremental scheme.
#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "support/json.h"

namespace core::state {

struct Scope {
  std::string skill;
  std::vector<std::string> caps;
  bool persistSession = false;  // true when persist: session
};

// Session-lifetime adjudication counters, shown by the statusline and the
// status command. Ridden on the existing per-tool-call state save in the
// pre-tool path — no extra I/O.
struct Counters {
  std::uint64_t allow = 0;
  std::uint64_t ask = 0;
  std::uint64_t deny = 0;
};

// The most recent denied call, composed from already-sanitized journal input
// (same redaction rules; content never enters state). std::nullopt until the
// first deny happens.
struct LastDenial {
  std::string tool;     // neutral tool name
  std::string summary;  // sanitized input summary, truncated to 48 chars
  std::string caps;     // comma-joined unauthorized capability names
  std::string t;        // ISO8601 UTC of the denial
};

struct State {
  std::string sessionId;
  std::vector<Scope> scopes;
  Counters counters;
  std::optional<LastDenial> lastDenial;
};

std::string dir(std::string_view root);

// Missing file returns a zero-valued State (not an error); a corrupt file returns nullopt.
std::optional<State> load(std::string_view root, std::string_view sessionId);
// Atomic write: tmp + rename. Returns false on failure (callers treat it as non-blocking).
bool save(std::string_view root, std::string_view sessionId, const State& s);

support::Json toJson(const State& s);
std::optional<State> fromJson(const support::Json& j, std::string& err);

}  // namespace core::state
