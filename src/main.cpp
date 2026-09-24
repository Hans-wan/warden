// warden CLI entry: hook subcommands + trajectory outlet + self-check +
// packaging helper. Every warden-internal failure is non-blocking for the
// host (exit 1 only means "warden itself did not run").
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

#include "adapters/claude-code/hooks.h"
#include "core/journal/journal.h"
#include "core/policy/policy.h"
#include "core/replay/diff.h"
#include "core/state/state.h"
#include "doctor/doctor.h"
#include "hooks_cmd.h"
#include "statusline/statusline.h"

namespace fs = std::filesystem;

namespace {

// The binary's own path (argv[0]), used by doctor and hints.
std::string selfPath;

void usage() {
  std::cerr <<
      "warden — runtime verification layer for Claude Code skills\n"
      "\n"
      "Hooks (invoked by hooks.json, not by hand):\n"
      "  session-start          create session state, ensure journal dir, write .gitignore\n"
      "  pre-tool               adjudicate this tool call (the only sync hook; may block)\n"
      "  post-tool              record a tool-result summary (async)\n"
      "  stop                   turn boundary: demote turn-tier capabilities and persist (async)\n"
      "\n"
      "Trajectories:\n"
      "  record <root> <sid>    mark an existing trajectory as baseline (prints counts)\n"
      "  replay <root> <sid>    print a trajectory's three-layer diff report\n"
      "  diff <root> <base> <tgt>   compare two trajectories; with --strict, no new\n"
      "                         capabilities exits 0, any new capability exits 1\n"
      "                         (CI gate); without --strict always exits 0\n"
      "\n"
      "Other:\n"
      "  gen-hooks              print hooks.json to stdout (shipped with the plugin)\n"
      "  statusline            render the one-line session status (for statusLine)\n"
      "  status [root]         multi-line report for the most recent session\n"
      "  doctor [root]          self-check: hooks mounted, actually fire, hot-path latency, journal writable, statusline usable\n";
}

// cwd convention: trajectory commands take root explicitly from the user
// (v1 has no global state directory).
int cmdDiff(int argc, char** argv) {
  // diff <root> <base> <target> [--strict]
  bool strict = false;
  std::vector<std::string> pos;
  for (int i = 2; i < argc; ++i) {
    const std::string a = argv[i];
    if (a == "--strict") {
      strict = true;
    } else {
      pos.push_back(a);
    }
  }
  if (pos.size() < 3) {
    std::cerr << "usage: warden diff <root> <base-session> <target-session> "
                 "[--strict]\n";
    return 2;
  }
  const std::string& root = pos[0];
  const auto base = core::journal::readAll(root, pos[1]);
  const auto target = core::journal::readAll(root, pos[2]);
  const core::replay::Report r = core::replay::diff(base, target);

  // The gate criterion = whether addedCaps is empty. **Never read
  // the verdict string**: on a mixed add+remove verdict is drift while
  // addedCaps is non-empty — reading the verdict would miss real privilege
  // expansion.
  std::cout << "verdict: " << r.verdict << "\n";
  std::cout << "added caps: ";
  for (std::size_t i = 0; i < r.addedCaps.size(); ++i) {
    if (i) std::cout << ", ";
    std::cout << r.addedCaps[i];
  }
  std::cout << (r.addedCaps.empty() ? "(none)" : "") << "\n";
  std::cout << "removed caps: ";
  for (std::size_t i = 0; i < r.removedCaps.size(); ++i) {
    if (i) std::cout << ", ";
    std::cout << r.removedCaps[i];
  }
  std::cout << (r.removedCaps.empty() ? "(none)" : "") << "\n";
  std::cout << "tool-sequence edit distance: " << r.toolSeqDist << "\n";
  std::cout << "product changes: ";
  for (std::size_t i = 0; i < r.productChanges.size(); ++i) {
    if (i) std::cout << ", ";
    std::cout << r.productChanges[i];
  }
  std::cout << (r.productChanges.empty() ? "(none)" : "") << "\n";

  if (strict && !r.addedCaps.empty()) return 1;  // privilege expansion -> CI fails
  return 0;  // non-strict only prints the report; strict with no additions also exits 0
}

int cmdRecord(int argc, char** argv) {
  if (argc < 4) {
    std::cerr << "usage: warden record <root> <session-id>\n";
    return 2;
  }
  const auto entries = core::journal::readAll(argv[2], argv[3]);
  if (entries.empty()) {
    std::cerr << "warden record: no trajectory read (" << argv[3] << ")\n";
    return 1;
  }
  const auto caps = core::replay::capSet(entries);
  std::cout << "recorded " << entries.size() << " event(s), capability set has "
            << caps.size() << " item(s)\n";
  for (const auto& c : caps) std::cout << "  " << c << "\n";
  return 0;
}

int cmdReplay(int argc, char** argv) {
  if (argc < 4) {
    std::cerr << "usage: warden replay <root> <session-id> [--baseline <sid>]\n";
    return 2;
  }
  const std::string root = argv[2];
  if (argc >= 6 && std::string(argv[4]) == "--baseline") {
    // With a baseline: emit the diff directly (same gate semantics as the
    // diff subcommand; non-strict by default).
    std::vector<char*> argv2{argv[0], const_cast<char*>("diff"),
                             const_cast<char*>(root.c_str()), argv[5], argv[3]};
    return cmdDiff(static_cast<int>(argv2.size()), argv2.data());
  }
  // Without a baseline: print this trajectory's report only.
  const auto entries = core::journal::readAll(root, argv[3]);
  const auto caps = core::replay::capSet(entries);
  std::cout << "trajectory " << argv[3] << ": " << entries.size() << " event(s)\n";
  std::cout << "capability set: ";
  for (std::size_t i = 0; i < caps.size(); ++i) {
    if (i) std::cout << ", ";
    std::cout << caps[i];
  }
  std::cout << (caps.empty() ? "(empty)" : "") << "\n";
  return 0;
}

int cmdDoctor(int argc, char** argv) {
  // root defaults to the current working directory (users run warden doctor
  // inside their own project).
  std::string root = ".";
  if (argc >= 3) root = argv[2];
  if (root == ".") {
    // Prefer the user's cwd: if it is explicitly wired (has its own
    // hooks/hooks.json) the report must be about that project, never about the
    // plugin that happens to contain the binary. Only when cwd is unwired does
    // the plugin-root walk apply — in the installed-plugin layout the binary
    // lives at <plugin>/bin/warden, so bare `warden doctor` self-checks against
    // the bundled hooks.json. Unwired cwd in a source tree finds no marker and
    // stays on ".". A relative path is fine here: hooks-configured joins it
    // with hooks/hooks.json relative to cwd anyway.
    std::error_code cwdEc;
    if (!fs::exists(fs::path(".") / "hooks" / "hooks.json", cwdEc)) {
      const std::string pluginRoot = core::doctor::findPluginRootPublic(selfPath);
      if (!pluginRoot.empty()) root = pluginRoot;
    }
  }
  const auto checks = core::doctor::run(root, selfPath);
  bool allOk = true;
  for (const auto& c : checks) {
    if (!c.ok) allOk = false;
    std::cout << (c.ok ? "[ok]   " : "[FAIL] ") << c.name << ": " << c.note
              << "\n";
  }
  std::cout << (allOk ? "doctor: all checks passed\n"
                      : "doctor: some checks failed — the gate may be failing silently\n");
  return allOk ? 0 : 1;
}

// statusline: reads the host statusline JSON on stdin and prints one line.
// Hard contract: exit 0 always, nothing on stderr, never an empty line —
// a broken statusline must degrade to a dim "observing", never flash red.
int cmdStatusline() {
  std::string in((std::istreambuf_iterator<char>(std::cin)),
                 std::istreambuf_iterator<char>());
  std::string err;
  auto j = support::Json::parse(in, err);
  std::string sid, cwd;
  if (err.empty() && j.is_object()) {
    auto s = j.find("session_id");
    if (s != j.end() && s->second.is_string()) sid = s->second.get<std::string>();
    auto w = j.find("workspace");
    if (w != j.end() && w->second.is_object()) {
      auto c = w->second.find("current_dir");
      if (c != w->second.end() && c->second.is_string()) cwd = c->second.get<std::string>();
    }
    if (cwd.empty()) {
      auto c = j.find("cwd");
      if (c != j.end() && c->second.is_string()) cwd = c->second.get<std::string>();
    }
  }
  const bool color = core::statusline::detectColor(std::getenv("NO_COLOR"));
  if (cwd.empty() || sid.empty()) {
    // Delegate the fallback to the renderer so the dim-observing form lives in
    // exactly one place.
    std::cout << core::statusline::renderOneLine(nullptr, nullptr, color)
              << "\n";
    return 0;
  }
  auto st = core::state::load(cwd, sid);
  if (!st) st = core::state::State{};  // corrupt -> zero state (observe)
  auto merged = core::policy::load({(fs::path(cwd) / ".warden" / "policy.yaml").string()});
  core::policy::Policy pol = merged ? *merged : core::policy::defaultPolicy();
  std::cout << core::statusline::renderOneLine(&*st, &pol, color) << "\n";
  return 0;
}

// status: multi-line report for the /warden:status slash command. Shows the
// most recently active session (state file with the newest mtime) — the
// interactive session is by definition the one just written by pre-tool.
int cmdStatus(int argc, char** argv) {
  std::string root = ".";
  if (argc >= 3) root = argv[2];
  std::error_code ec;
  const fs::path sdir = fs::path(root) / ".warden" / "state";
  std::string newestSid;
  fs::file_time_type newest{};
  if (fs::exists(sdir, ec)) {
    for (const auto& e : fs::directory_iterator(sdir, ec)) {
      if (e.path().extension().string() != ".json") continue;
      auto t = e.last_write_time(ec);
      if (ec) continue;
      if (newestSid.empty() || t > newest) {
        newest = t;
        newestSid = e.path().stem().string();
      }
    }
  }
  if (newestSid.empty()) {
    std::cout << "warden: no sessions recorded under " << sdir.string()
              << "\nIs the plugin installed and has a session run here?\n";
    return 1;
  }
  auto st = core::state::load(root, newestSid);
  if (!st) {
    std::cout << "warden: state file for session " << newestSid
              << " is corrupt\n";
    return 1;
  }
  auto merged =
      core::policy::load({(fs::path(root) / ".warden" / "policy.yaml").string()});
  core::policy::Policy pol = merged ? *merged : core::policy::defaultPolicy();
  const fs::path journalPath =
      fs::path(core::journal::dir(root)) / (newestSid + ".jsonl");
  const bool color = core::statusline::detectColor(std::getenv("NO_COLOR"));
  std::cout << core::statusline::renderReport(*st, pol, journalPath.string(), color)
            << "\n";
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  selfPath = argc > 0 ? argv[0] : "warden";
  if (argc < 2) {
    usage();
    return 2;
  }
  const std::string sub = argv[1];

  if (sub == "session-start") return warden::sessionStart();
  if (sub == "pre-tool") return warden::preTool();
  if (sub == "post-tool") return warden::postTool();
  if (sub == "stop") return warden::stop();

  if (sub == "gen-hooks") {
    // The plugin ships bin/warden-launcher (a POSIX sh platform picker);
    // hooks command the launcher, which resolves the per-platform binary
    // itself. The template keeps the variable for the host to expand at
    // plugin load.
    std::cout << cc::hooksJson("${CLAUDE_PLUGIN_ROOT}/bin/warden-launcher");
    return 0;
  }
  if (sub == "statusline") return cmdStatusline();
  if (sub == "status") return cmdStatus(argc, argv);
  if (sub == "doctor") return cmdDoctor(argc, argv);
  if (sub == "diff") return cmdDiff(argc, argv);
  if (sub == "record") return cmdRecord(argc, argv);
  if (sub == "replay") return cmdReplay(argc, argv);

  usage();
  return 2;
}
