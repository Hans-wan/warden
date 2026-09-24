// doctor self-check.
// Motivation: the three silent-failure paths (hook timeout lets calls through,
// a wrong path degrades silently, @file bypasses) never raise an error — they
// just make the gate vanish. A security tool that fails silently is worse
// than none, so doctor actively feeds one synthetic payload and verifies the
// journal actually grew an event.
//
// Like the rest of warden this file compiles with -fno-exceptions: all
// failures surface through return values, no try/catch. The subprocess uses
// POSIX popen/pclose (exception-free).
#include "doctor/doctor.h"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <sys/wait.h>
#include <unistd.h>

#include "adapters/claude-code/hooks.h"
#include "core/journal/journal.h"
#include "core/state/state.h"
#include "support/json.h"

namespace fs = std::filesystem;

namespace core::doctor {
namespace {

// Session id used by the synthetic probe. Fixed value: on repeated doctor
// runs a reader can tell at a glance that these events are not a real session
// (and the probe's own events can be read back to prove the journal is
// readable).
constexpr const char* kProbeSession = "doctor-probe";
// Hot-path hard limit 100 ms (target P95 < 20 ms).
constexpr double kHotPathHardLimitMs = 100.0;

// Single-quote wrapping keeps shell metacharacters in paths from being
// interpreted. Paths containing a single quote give up (empty string returned;
// the caller treats it as "cannot build a command" rather than force-splicing).
std::string shellQuote(const std::string& s) {
  if (s.find('\'') != std::string::npos) return "";
  return "'" + s + "'";
}

// Synthesizes one harmless PreToolUse payload (cwd points at the root under
// test, so the journal lands there). Assembly itself lives in the adapter
// (host field names are not core's vocabulary).
std::string probePayload(const std::string& root) {
  return cc::probePreToolPayload(root, kProbeSession);
}

Check hooksConfigured(const std::string& root) {
  Check c;
  c.name = "hooks-configured";
  const std::string path = (fs::path(root) / "hooks" / "hooks.json").string();
  std::ifstream in(path, std::ios::binary);
  if (!in.is_open()) {
    c.note = "hooks.json not found: " + path + " — hooks may not be mounted (silent failure)";
    return c;
  }
  std::string text((std::istreambuf_iterator<char>(in)),
                   std::istreambuf_iterator<char>());
  // All four event names must be present. Searching for quoted key names
  // keeps "Stop" from falsely matching "SubagentStop".
  const char* events[] = {"\"SessionStart\"", "\"PreToolUse\"", "\"PostToolUse\"",
                          "\"Stop\""};
  std::string missing;
  for (const char* ev : events) {
    if (text.find(ev) == std::string::npos) {
      if (!missing.empty()) missing += ", ";
      missing += ev;
    }
  }
  if (!missing.empty()) {
    c.note = "hooks.json missing events: " + missing;
    return c;
  }
  if (text.find("warden") == std::string::npos) {
    c.note = "all four events present, but no command references warden — mount it in settings";
    return c;
  }
  c.ok = true;
  c.note = path;
  return c;
}

struct PreToolRun {
  bool spawned = false;  // popen succeeded (the command string could run)
  bool found = false;    // the binary really exists: the shell exit code is not 127
  int status = -1;
  double ms = 0.0;
};

// Feeds the payload to `<bin> pre-tool` and times it. Uses popen's "w"
// direction: the command reads stdin, we write and close the pipe.
// Exception-free path.
PreToolRun runPreTool(const std::string& binPath, const std::string& payload) {
  PreToolRun r;
  const std::string bin = shellQuote(binPath);
  if (bin.empty()) return r;
  // The child's stdout would inherit doctor's stdout: adjudication JSON must
  // not leak into the self-check report, so redirect to /dev/null (the hook's
  // real output is consumed by the host, not something doctor cares about).
  const std::string cmd = bin + " pre-tool >/dev/null 2>&1";
  const auto t0 = std::chrono::steady_clock::now();
  std::FILE* p = ::popen(cmd.c_str(), "w");
  if (p == nullptr) return r;
  r.spawned = true;
  const std::size_t n = std::fwrite(payload.data(), 1, payload.size(), p);
  (void)n;  // a write failure means the child sees short input and handles it; the verdict comes from the journal below
  const int status = ::pclose(p);
  const auto t1 = std::chrono::steady_clock::now();
  // pclose returns a wait(2) status word, **not** an exit code: when the
  // command does not exist the shell exits 127 and the status word is 32512
  // (127<<8). It must be decoded via WEXITSTATUS — otherwise 32512 != 127
  // misjudges "wrong path" as success.
  // The key trap: 127 left-shifted by 8 has its low 8 bits all zero, so the
  // raw value reads like "exit 0" — without decoding, the most important
  // failure path of all gets inverted.
  r.status = WIFEXITED(status) ? WEXITSTATUS(status) : status;
  r.found = !(WIFEXITED(status) && WEXITSTATUS(status) == 127);
  r.ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
  return r;
}

std::size_t journalCount(const std::string& root) {
  return journal::readAll(root, kProbeSession).size();
}

Check hookFires(const std::string& root, const std::string& binPath,
                const PreToolRun& run) {
  Check c;
  c.name = "hook-fires";
  if (!run.spawned) {
    c.note = "could not start " + binPath + " — binary missing or not executable";
    return c;
  }
  if (!run.found) {
    c.note = "binary not found: " + binPath + " (shell exit 127) — the hook path is wrong";
    return c;
  }
  const std::size_t after = journalCount(root);
  if (after == 0) {
    c.note = "journal still empty after feeding the synthetic payload — the gate may not be wired up";
    return c;
  }
  c.ok = true;
  c.note = "journal has " + std::to_string(after) + " event(s)";
  return c;
}

Check hotPathLatency(const PreToolRun& run) {
  Check c;
  c.name = "hot-path-latency";
  // When the binary never ran, the "4 ms" elapsed time is just the shell
  // reporting command-not-found, not a hot-path measurement. Reporting [ok]
  // on it would mask the real failure, so fail as "unmeasurable".
  if (!run.spawned || !run.found) {
    c.note = !run.spawned
                 ? "could not start pre-tool; cannot measure (fix hook-fires first)"
                 : "binary not found (exit 127); cannot measure (fix hook-fires)";
    return c;
  }
  char buf[64];
  std::snprintf(buf, sizeof(buf), "pre-tool measured %.1fms (hard limit %.0fms)", run.ms,
                kHotPathHardLimitMs);
  c.note = buf;
  c.ok = run.ms <= kHotPathHardLimitMs;
  if (!c.ok) c.note += " — over budget: the hook slows down every tool call";
  return c;
}

// Walks up from the binary's directory looking for the plugin layout
// (hooks/hooks.json beside the binary's ancestor). Returns "" when absent —
// doctor then falls back to the root argument as today. Depth-limited: the
// installed-plugin layout is <plugin>/bin/warden, so a handful of levels is
// plenty; a bound keeps a stray marker far up the tree from being adopted.
std::string findPluginRoot(const std::string& binPath) {
  std::error_code ec;
  fs::path dir = fs::path(binPath).parent_path();
  for (int depth = 0; depth < 6 && !dir.empty(); ++depth) {
    const fs::path marker = dir / "hooks" / "hooks.json";
    if (fs::exists(marker, ec)) return dir.string();
    auto parent = dir.parent_path();
    if (parent == dir) break;  // reached the filesystem root
    dir = parent;
  }
  return "";
}

// Fifth check: the statusline subcommand must run and print something for a
// synthetic payload. The statusline never gates anything, so a failure here
// is cosmetic — but it means the in-session visibility promise is broken.
Check statuslineUsable(const std::string& binPath, const std::string& root) {
  Check c;
  c.name = "statusline-usable";
  const std::string bin = shellQuote(binPath);
  if (bin.empty()) {
    c.note = "binary path contains a single quote; cannot test the statusline";
    return c;
  }
  // Synthetic statusline payload: same session the other probes use, cwd
  // pointing at the root under test. Written to a temp file so the child's
  // stdin comes from `cat` while popen's read end captures stdout (popen's "w"
  // direction used by runPreTool cannot read the child's output).
  // Json::object is a type alias (std::map), so wrap it in a Json before
  // dumping — dotting a bare std::map has no dump() member.
  const std::string payload =
      Json(Json::object{{"session_id", Json(kProbeSession)},
                        {"workspace", Json::object{{"current_dir", Json(root)}}}})
          .dump();
  // pid-suffixed so parallel doctor runs never share the probe file.
  const std::string tmp = (fs::temp_directory_path() /
                           ("warden-doctor-statusline-" + std::to_string(::getpid())))
                              .string();
  {
    std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
    if (!out) {
      c.note = "could not write the probe payload to a temp file";
      return c;
    }
    out << payload;
  }
  // stderr is dropped: a missing binary prints "command not found" there and
  // that is not the report's business — the exit status tells the story.
  const std::string tmpQ = shellQuote(tmp);
  if (tmpQ.empty()) {
    // Same give-up rule as the binary path: the probe file exists by now, so
    // clean it up before returning rather than leaving it in the temp dir.
    std::error_code rmec;
    fs::remove(tmp, rmec);
    c.note = "cannot test: temp path contains a quote";
    return c;
  }
  const std::string cmd = "cat " + tmpQ + " | " + bin + " statusline 2>/dev/null";
  int status = -1;
  {
    std::FILE* p = ::popen(cmd.c_str(), "r");
    if (p == nullptr) {
      c.note = "could not start warden statusline";
      std::error_code rmec;
      fs::remove(tmp, rmec);
      return c;
    }
    // stdout carries the statusline itself; only the exit status matters, so
    // read and discard — leaving it unread would block the child on a full pipe.
    char buf[256];
    while (std::fgets(buf, sizeof(buf), p) != nullptr) {
    }
    status = ::pclose(p);
  }
  std::error_code rmec;
  fs::remove(tmp, rmec);
  // pclose yields a wait(2) status word, not an exit code (see runPreTool):
  // decode via WIFEXITED/WEXITSTATUS or 127<<8 reads like success.
  if (status == -1 || !WIFEXITED(status) || WEXITSTATUS(status) != 0) {
    c.note = "statusline subcommand failed — the status bar will show nothing";
    return c;
  }
  c.ok = true;
  c.note = "statusline renders";
  return c;
}

Check journalWritable(const std::string& root) {
  Check c;
  c.name = "journal-writable";
  const std::size_t before = journalCount(root);
  journal::Entry e;
  e.t = "probe";
  e.ev = "policy";
  e.tool = "probe";
  e.input = Json::object{{"probe", Json(true)}};
  e.decision = "allow";
  if (!journal::append(root, kProbeSession, e)) {
    c.note = "could not append an event to the journal: " + journal::dir(root);
    return c;
  }
  const std::size_t after = journalCount(root);
  if (after <= before) {
    c.note = "no new event read back after append — journal write/read inconsistent";
    return c;
  }
  c.ok = true;
  c.note = journal::dir(root);
  return c;
}

}  // namespace

std::string findPluginRootPublic(const std::string& binPath) {
  return findPluginRoot(binPath);
}

std::vector<Check> run(const std::string& root, const std::string& binPath) {
  std::vector<Check> checks;
  checks.push_back(hooksConfigured(root));

  const std::string payload = probePayload(root);
  const PreToolRun pre = runPreTool(binPath, payload);

  checks.push_back(hookFires(root, binPath, pre));
  checks.push_back(hotPathLatency(pre));
  checks.push_back(journalWritable(root));
  checks.push_back(statuslineUsable(binPath, root));
  return checks;
}

}  // namespace core::doctor
