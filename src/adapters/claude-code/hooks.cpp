// hooks.json generation (shipped with the plugin; zero user configuration).
// The sync/async split is a hard architectural constraint: pre-tool must be
// synchronous (it blocks on the hot path), while post-tool and stop carry
// "async": true (async command hooks cannot block; they exist to record).
// SessionEnd is never declared — it has a 1.5 s budget, too short to write
// out a trajectory.
#include "adapters/claude-code/hooks.h"

#include <string>

#include "support/json.h"

namespace cc {
namespace {

// This file hand-writes JSON text instead of serializing a whole tree with
// support::Json: json.h's objects sit on a std::map, whose **keys sort
// lexicographically** — "async" would land before "command" and PostToolUse
// before PreToolUse, losing the hand-written field order (also the
// order a human-readable bundled config should have). Json is borrowed here
// only for string escaping (Json::dump()); key order follows writing order.
//
// Note: the host reads shape-correct-but-misordered JSON fine; this fix
// matters so the bundled hooks.json stays byte-stable
// (diffable, reviewable), not as a functional repair.
std::string str(const std::string& s) { return Json(s).dump(); }

// One hook entry: command first; async appears only when needed (a synchronous
// entry with "async": false is meaningless, and this format has no such
// key on pre-tool — kept verbatim).
std::string commandHook(const std::string& bin, const std::string& sub,
                        bool async, int depth) {
  const std::string pad(static_cast<std::size_t>(depth) * 2, ' ');
  const std::string padIn(static_cast<std::size_t>(depth + 1) * 2, ' ');
  std::string out = "{\n";
  out += padIn + str("type") + ": " + str("command") + ",\n";
  out += padIn + str("command") + ": " + str(bin + " " + sub);
  if (async) out += ",\n" + padIn + str("async") + ": true";
  out += "\n" + pad + "}";
  return out;
}

// Event array: matcher first, hooks after (fixed order).
std::string eventArray(const std::string& bin, const std::string& sub,
                       bool async, int depth) {
  const std::string pad(static_cast<std::size_t>(depth) * 2, ' ');
  const std::string padIn(static_cast<std::size_t>(depth + 1) * 2, ' ');
  std::string out = "[\n";
  out += padIn + "{\n";
  out += padIn + "  " + str("matcher") + ": " + str("*") + ",\n";
  out += padIn + "  " + str("hooks") + ": [\n";
  out += padIn + "    " + commandHook(bin, sub, async, depth + 3) + "\n";
  out += padIn + "  ]\n";
  out += padIn + "}\n";
  out += pad + "]";
  return out;
}

// One event block: `"<Event>": <array>`.
std::string eventBlock(const std::string& name, const std::string& arr) {
  return "    " + str(name) + ": " + arr;
}

}  // namespace

std::string hooksJson(const std::string& binPath) {
  // Event and field order: SessionStart / PreToolUse /
  // PostToolUse / Stop; each hook is type / command / async.
  std::string out = "{\n";
  out += "  " + str("hooks") + ": {\n";
  // Session start: create state, ensure the journal dir, write .gitignore. Sync, non-blocking.
  out += eventBlock("SessionStart",
                    eventArray(binPath, "session-start", false, 2)) +
         ",\n";
  // The only synchronous entry: adjudication must block on the hot path
  // **No async key** — async hooks cannot block, and a pre-tool
  // that runs async would be a gate in name only.
  out += eventBlock("PreToolUse", eventArray(binPath, "pre-tool", false, 2)) +
         ",\n";
  // Async: records a tool-result summary only, never blocks (PostToolUse's
  // tool_response carries sensitive host content and stays out of the
  // journal — the privacy red line).
  out += eventBlock("PostToolUse",
                    eventArray(binPath, "post-tool", true, 2)) +
         ",\n";
  // Async: turn boundary; demote turn-tier capabilities and persist.
  out += eventBlock("Stop", eventArray(binPath, "stop", true, 2)) + "\n";
  out += "  }\n";
  out += "}\n";
  return out;
}

std::string probePreToolPayload(const std::string& cwd,
                                const std::string& sessionId) {
  Json input = Json::object{{"command", Json("true")}};
  Json p = Json::object{
      {"session_id", Json(sessionId)},
      {"cwd", Json(cwd)},
      {"permission_mode", Json("default")},
      {"hook_event_name", Json("PreToolUse")},
      {"tool_name", Json("Bash")},
      {"tool_input", std::move(input)},
  };
  return p.dump();
}

}  // namespace cc
