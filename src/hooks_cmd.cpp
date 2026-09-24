// Implementations of the four hook subcommands (session-start / pre-tool / post-tool / stop).
// Host-specific field
// names may appear only through the adapters layer.
#include "hooks_cmd.h"

#include <algorithm>
#include <cctype>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

#include "adapters/claude-code/output.h"
#include "adapters/claude-code/payload.h"
#include "core/attribution/scope.h"
#include "core/capability/capability.h"
#include "core/journal/journal.h"
#include "core/policy/policy.h"
#include "core/state/state.h"
#include "core/toolcall/toolcall.h"
#include "support/json.h"

namespace fs = std::filesystem;

namespace warden {

std::string skillDeclPath(const std::string& cwd) {
  return (fs::path(cwd) / ".warden" / "policy.yaml").string();
}

std::vector<std::pair<std::string, SkillDecl>> parseSkillDecls(
    const std::string& yamlText);

namespace {

// ---- Shared small utilities -----------------------------------------------

// Reads all of stdin. Empty or oversized input is not an error here; parsing
// decides (parsePayload).
std::string readStdin() {
  return std::string((std::istreambuf_iterator<char>(std::cin)),
                     std::istreambuf_iterator<char>());
}

// ISO8601 UTC. The journal never reads the clock; the caller injects time
// (see journal.h).
std::string nowIso() {
  const std::time_t t = std::time(nullptr);
  std::tm tm{};
  gmtime_r(&t, &tm);
  char buf[32];
  std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
  return buf;
}

std::string lower(std::string_view s) {
  std::string out(s);
  std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return out;
}

std::string rtrim(std::string_view s) {
  std::size_t n = s.size();
  while (n > 0 && (s[n - 1] == '\r' || s[n - 1] == ' ' || s[n - 1] == '\t')) --n;
  return std::string(s.substr(0, n));
}

std::string ltrim(std::string_view s) {
  std::size_t i = 0;
  while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) ++i;
  return std::string(s.substr(i));
}

// Dedupes and sorts a caps string list (deterministic: the state and journal
// read-back contracts both benefit).
std::vector<std::string> sortedUnique(std::vector<std::string> v) {
  std::sort(v.begin(), v.end());
  v.erase(std::unique(v.begin(), v.end()), v.end());
  return v;
}

// sessionId path validation (final safety net). session_id is a host-controlled
// field and gets spliced into .warden/journal/<sid>.jsonl and
// .warden/state/<sid>.json; "../../evil" would escape .warden/. This is the
// outermost primary check; core (journal/state) holds private copies with the
// same semantics for defense in depth. Only [A-Za-z0-9._-] passes, and
// "." / ".." are excluded.
bool validSessionId(std::string_view sessionId) {
  if (sessionId.empty()) return false;
  if (sessionId == "." || sessionId == "..") return false;
  for (char c : sessionId) {
    const bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                    (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-';
    if (!ok) return false;  // covers '/', '\\', and everything else
  }
  return true;
}

// ---- Minimal YAML parsing (serves the skills: section only) ----------------
// Recognizes only these shapes (2-space indent):
//   skills:
//     pdf-tools:
//       capabilities: [fs:read, shell]
//       capabilities:
//         - fs:read
//       persist: session
// Unknown keys and unrecognizable syntax are ignored: policy-file tolerance
// beats strictness (matching the policy module's handling of unknown
// capability tags). **This parser lives only in this file** — the core policy
// module is untouched by it (separation of concerns).

std::string stripComment(std::string_view line) {
  const std::size_t h = line.find('#');
  if (h == std::string_view::npos) return std::string(line);
  return std::string(line.substr(0, h));
}

std::size_t indentOf(std::string_view line) {
  std::size_t i = 0;
  while (i < line.size() && line[i] == ' ') ++i;
  return i;
}

// Normalizes "foo" / 'foo' / foo into foo.
std::string unquote(std::string s) {
  const std::string t = rtrim(ltrim(s));
  if (t.size() >= 2 && ((t.front() == '"' && t.back() == '"') ||
                        (t.front() == '\'' && t.back() == '\''))) {
    return t.substr(1, t.size() - 2);
  }
  return t;
}

std::string valueAfterColon(std::string_view line) {
  const std::size_t c = line.find(':');
  if (c == std::string_view::npos) return "";
  return std::string(line.substr(c + 1));
}

// The inline form of "capabilities: [a, b]".
void parseInlineList(const std::string& raw, std::vector<std::string>* out) {
  std::string v = ltrim(raw);
  if (!v.empty() && v.front() == '[') {
    v.erase(v.begin());
    const std::size_t e = v.find(']');
    if (e != std::string::npos) v = v.substr(0, e);
  }
  std::string cur;
  for (char ch : v) {
    if (ch == ',') {
      const std::string c = unquote(cur);
      if (!c.empty()) out->push_back(c);
      cur.clear();
    } else {
      cur.push_back(ch);
    }
  }
  const std::string c = unquote(cur);
  if (!c.empty()) out->push_back(c);
}

// Reads the skills declarations from <cwd>/.warden/policy.yaml; missing file
// yields an empty table (not an error).
std::vector<std::pair<std::string, SkillDecl>> loadSkillDecls(
    const std::string& cwd) {
  std::ifstream in(skillDeclPath(cwd));
  if (!in.is_open()) return {};
  std::string text((std::istreambuf_iterator<char>(in)),
                   std::istreambuf_iterator<char>());
  return parseSkillDecls(text);
}

const SkillDecl* findDecl(
    const std::vector<std::pair<std::string, SkillDecl>>& decls,
    const std::string& skill) {
  for (const auto& d : decls)
    if (d.first == skill) return &d.second;
  return nullptr;
}

// Strictest mode: Observe < Ask < Enforce.
core::policy::Mode strictest(core::policy::Mode a, core::policy::Mode b) {
  using M = core::policy::Mode;
  auto rank = [](M m) {
    switch (m) {
      case M::Enforce: return 2;
      case M::Ask: return 1;
      case M::Observe: return 0;
    }
    return 0;
  };
  return rank(a) >= rank(b) ? a : b;
}

// Loads policy: factory defaults + project overrides. Corrupt -> fall back to
// factory defaults (non-blocking: a broken policy must not stall the user's
// work; doctor exists so silent degradation can be discovered).
core::policy::Policy loadPolicy(const std::string& cwd) {
  auto merged = core::policy::load({skillDeclPath(cwd)});
  if (merged) return *merged;
  return core::policy::defaultPolicy();
}

// Unauthorized capabilities = those among this call's tags that no open
// scope authorizes.
std::vector<std::string> unauthorized(
    const std::vector<core::cap::Tag>& caps,
    const std::vector<std::string>& active) {
  std::vector<std::string> out;
  for (core::cap::Tag t : caps) {
    const std::string n(core::cap::name(t));
    if (std::find(active.begin(), active.end(), n) == active.end())
      out.push_back(n);
  }
  return out;
}

// Deny/ask reason (itemized format): offending capabilities +
// likely responsible scopes.
std::string reasonFor(const std::vector<std::string>& missing,
                      const std::vector<std::string>& responsible) {
  std::string r = "capability ";
  for (std::size_t i = 0; i < missing.size(); ++i) {
    if (i) r += ", ";
    r += missing[i];
  }
  r += " not authorized by any open scope";
  if (responsible.empty()) {
    r += "; no open scopes (user-level base policy applies)";
  } else {
    r += "; responsible: [";
    const std::size_t n = std::min<std::size_t>(responsible.size(), 3);
    for (std::size_t i = 0; i < n; ++i) {
      if (i) r += ", ";
      r += responsible[i];
    }
    if (responsible.size() > n) r += ", ...";
    r += "]";
  }
  return r;
}

// The humane companion line: tells the user what happened and what to do,
// in plain words, after the machine-readable line (which stays first and
// byte-identical for CI consumers). `missing` is already deduped/sorted by
// unauthorized(), so the joined list is deterministic.
std::string humaneFor(const std::string& decision,
                      const std::vector<std::string>& missing) {
  std::string caps;
  for (std::size_t i = 0; i < missing.size(); ++i) {
    if (i) caps += ", ";
    caps += missing[i];
  }
  if (decision == "deny") {
    return "warden blocked this call: the active skill did not declare " + caps +
           ". Run /warden:status for the full picture, or add " + caps +
           " to the skill in .warden/policy.yaml if this is expected.";
  }
  return "warden flagged this call for review (needs " + caps +
         ") — approve if it looks right. Run /warden:status for details.";
}

// Names of currently open scopes (in state order).
std::vector<std::string> scopeNames(const core::state::State& st) {
  std::vector<std::string> out;
  for (const auto& sc : st.scopes) out.push_back(sc.skill);
  return out;
}

// String form of the capability-set judgment (deduped, sorted, deterministic).
std::vector<std::string> capNames(const std::vector<core::cap::Tag>& caps) {
  std::vector<std::string> out;
  for (core::cap::Tag t : caps) out.emplace_back(core::cap::name(t));
  return sortedUnique(std::move(out));
}

// One-line human summary of a denied call, composed from already-redacted
// journal input (the privacy red line applies here too: Write/Edit never
// leak content, Read never leaks anything but the path). Truncated to 48
// characters so the statusline stays single-line.
std::string denySummary(const core::ToolCall& tc) {
  support::Json s = core::journal::sanitizedInput(tc);
  std::string text;
  if (tc.name == core::tc::Bash) {
    text = tc.str("command");
  } else if (!tc.filePath().empty()) {
    text = tc.filePath();
  } else {
    // Unknown/MCP shapes: dump the sanitized object as-is. (The dump keeps its
    // braces; readability comes from the 48-char truncation below, not from
    // stripping delimiters.)
    text = s.dump();
  }
  if (text.size() > 48) {
    text.resize(45);
    text += "...";
  }
  return text;
}

// Appends one tool event. input is already sanitized.
void journalTool(const cc::Payload& p, const core::ToolCall& tc,
                 const std::vector<core::cap::Tag>& caps,
                 const std::vector<std::string>& scopes,
                 const std::string& decision, double durMs) {
  core::journal::Entry e;
  e.t = nowIso();
  e.ev = "tool";
  e.tool = tc.name;
  e.scopes = scopes;
  e.caps = capNames(caps);
  e.input = core::journal::sanitizedInput(tc);
  e.decision = decision;
  e.durMs = durMs;
  core::journal::append(p.cwd, p.sessionId, e);
}

}  // namespace

// Parses the skills: section. Indent semantics: skill keys sit at
// skillsIndent+2, their properties deeper.
std::vector<std::pair<std::string, SkillDecl>> parseSkillDecls(
    const std::string& yamlText) {
  std::vector<std::pair<std::string, SkillDecl>> out;
  std::istringstream lines(yamlText);
  std::string line;
  bool inSkills = false;
  std::size_t skillsIndent = 0;
  std::string curSkill;
  SkillDecl curDecl;
  bool inCapsList = false;
  std::size_t capsIndent = 0;

  auto flush = [&]() {
    if (!curSkill.empty()) out.emplace_back(curSkill, curDecl);
    curSkill.clear();
    curDecl = SkillDecl{};
  };

  while (std::getline(lines, line)) {
    const std::string noComment = stripComment(line);
    if (rtrim(noComment).empty()) continue;
    const std::size_t ind = indentOf(noComment);
    const std::string body = ltrim(noComment);

    if (!inSkills) {
      if (body == "skills:") {
        inSkills = true;
        skillsIndent = ind;
      }
      continue;
    }
    if (ind <= skillsIndent) {  // leaving the skills section
      flush();
      inSkills = false;
      continue;
    }

    // List-style capability item: `- fs:read`
    if (!body.empty() && body.front() == '-' && inCapsList && ind > capsIndent) {
      const std::string c = unquote(rtrim(ltrim(body.substr(1))));
      if (!c.empty()) curDecl.caps.push_back(c);
      continue;
    }

    const std::size_t colon = body.find(':');
    if (colon == std::string::npos) continue;
    const std::string key = unquote(body.substr(0, colon));
    const std::string val = valueAfterColon(body);

    if (ind <= skillsIndent + 2) {  // a skill name
      flush();
      inCapsList = false;
      curSkill = key;
      continue;
    }

    // Skill properties
    inCapsList = false;
    if (key == "capabilities") {
      parseInlineList(val, &curDecl.caps);
      if (ltrim(val).empty()) {
        inCapsList = true;
        capsIndent = ind;
      }
    } else if (key == "persist") {
      curDecl.persist = (lower(unquote(val)) == "session");
    }
  }
  flush();
  return out;
}

// ---- The four hook subcommands --------------------------------------------

// SessionStart: creates state, ensures the journal dir, and appends
// .warden/journal/ to .gitignore (making the privacy red line "journals are
// not committed to git" real).
int sessionStart() {
  std::string err;
  auto p = cc::parsePayload(readStdin(), err);
  if (!p) cc::failNonBlocking("warden session-start: payload parse failed: " + err);
  if (p->cwd.empty()) cc::failNonBlocking("warden session-start: payload missing cwd");
  if (!validSessionId(p->sessionId))
    cc::failNonBlocking("warden session-start: illegal session_id: " + p->sessionId);

  // 1. Create (or keep) the session state file. load returns a zero state
  // when the file is missing, so save is always safe here.
  auto st = core::state::load(p->cwd, p->sessionId);
  if (!st) cc::failNonBlocking("warden session-start: state corrupt, cannot initialize");
  st->sessionId = p->sessionId;
  core::state::save(p->cwd, p->sessionId, *st);

  // 2. Ensure the journal directory exists (an empty append is the wrong tool;
  // create the directory directly).
  std::error_code ec;
  fs::create_directories(core::journal::dir(p->cwd), ec);

  // 3. Append `.warden/journal/` to .gitignore (no-op if the line exists).
  const std::string ignorePath = (fs::path(p->cwd) / ".gitignore").string();
  const std::string entry = ".warden/journal/";
  std::string existing;
  {
    std::ifstream in(ignorePath, std::ios::binary);
    if (in.is_open()) {
      existing.assign((std::istreambuf_iterator<char>(in)),
                      std::istreambuf_iterator<char>());
    }
  }
  bool already = false;
  {
    std::istringstream ls(existing);
    std::string l;
    while (std::getline(ls, l)) {
      if (rtrim(l) == entry) {
        already = true;
        break;
      }
    }
  }
  if (!already) {
    std::ofstream out(ignorePath, std::ios::app | std::ios::binary);
    if (out.is_open()) {
      // A leading newline keeps the format intact: if the original file does
      // not end with a newline, the entry must not glue onto the last line.
      if (!existing.empty() && existing.back() != '\n') out << "\n";
      out << entry << "\n";
      out.flush();
    }
  }
  cc::emit("{}");  // SessionStart produces no adjudication output
  return 0;
}

// PreToolUse: the only synchronous hook. Hot-path hard constraint — a single
// process may only read state + read policy + parse stdin once + append to
// the journal once.
int preTool() {
  std::string err;
  auto p = cc::parsePayload(readStdin(), err);
  // Every warden-internal failure is non-blocking: better to miss
  // a block than to wreck the session.
  if (!p) cc::failNonBlocking("warden pre-tool: payload parse failed: " + err);
  if (p->cwd.empty() || p->sessionId.empty())
    cc::failNonBlocking("warden pre-tool: payload missing cwd/session_id");
  if (!validSessionId(p->sessionId))
    cc::failNonBlocking("warden pre-tool: illegal session_id: " + p->sessionId);

  const core::ToolCall tc = cc::toNeutral(*p);
  const std::vector<core::cap::Tag> caps = core::cap::of(tc);

  auto stOpt = core::state::load(p->cwd, p->sessionId);
  if (!stOpt) cc::failNonBlocking("warden pre-tool: state corrupt");
  core::state::State st = *stOpt;
  st.sessionId = p->sessionId;

  // A skill call opens a scope. The skill-name key path is tool_input.skill
  
  const std::string skill = tc.str("skill");
  if (tc.name == core::tc::Skill && !skill.empty()) {
    const auto decls = loadSkillDecls(p->cwd);
    const SkillDecl* d = findDecl(decls, skill);
    const std::vector<std::string> declared =
        d ? sortedUnique(d->caps) : std::vector<std::string>{};
    const bool persist = d ? d->persist : false;  // undeclared -> observe semantics
    core::attr::enter(st, skill, declared, persist);
    // No save here: the single per-call save on the adjudication path below
    // already persists this scope along with the updated counters.
  }

  const std::vector<std::string> active = core::attr::activeCaps(st);

  // Strictest mode across caps. With no capabilities (a skill call itself,
  // an unknown tool) there is nothing to adjudicate -> allow.
  core::policy::Policy pol = loadPolicy(p->cwd);
  core::policy::Mode mode = core::policy::Mode::Observe;
  for (core::cap::Tag t : caps) mode = strictest(mode, core::policy::decide(pol, t));

  const std::vector<std::string> missing = unauthorized(caps, active);
  const std::vector<std::string> scopes = scopeNames(st);

  if (!missing.empty() && mode == core::policy::Mode::Enforce) {
    const std::string reason = reasonFor(missing, core::attr::responsible(st));
    journalTool(*p, tc, caps, scopes, "deny", 0);
    st.counters.deny += 1;
    core::state::LastDenial ld;
    ld.tool = tc.name;
    ld.summary = denySummary(tc);
    for (std::size_t i = 0; i < missing.size(); ++i) {
      if (i) ld.caps += ",";
      ld.caps += missing[i];
    }
    ld.t = nowIso();
    st.lastDenial = std::move(ld);
    core::state::save(p->cwd, p->sessionId, st);
    cc::emit(cc::deny(reason, humaneFor("deny", missing)));
    return 0;  // Explicit return: no reliance on emit's exit side effect (emit is [[noreturn]])
  }
  if (!missing.empty() && mode == core::policy::Mode::Ask) {
    const std::string reason = reasonFor(missing, core::attr::responsible(st));
    st.counters.ask += 1;
    core::state::save(p->cwd, p->sessionId, st);
    journalTool(*p, tc, caps, scopes, "ask", 0);
    cc::emit(cc::ask(reason, humaneFor("ask", missing)));
    return 0;  // Same as above
  }

  st.counters.allow += 1;
  core::state::save(p->cwd, p->sessionId, st);
  journalTool(*p, tc, caps, scopes, "allow", 0);
  cc::emit(cc::allow(""));
  return 0;
}

// PostToolUse: records a tool-result summary only (async hook, must not block).
int postTool() {
  std::string err;
  auto p = cc::parsePayload(readStdin(), err);
  if (!p) cc::failNonBlocking("warden post-tool: payload parse failed: " + err);
  if (!validSessionId(p->sessionId))
    cc::failNonBlocking("warden post-tool: illegal session_id: " + p->sessionId);

  const core::ToolCall tc = cc::toNeutral(*p);
  core::journal::Entry e;
  e.t = nowIso();
  e.ev = "tool";
  e.tool = tc.name;
  // PostToolUse's tool_response carries sensitive host content (file
  // contents, command output) and never enters the journal — the privacy
  // red line. The summary only says "the tool finished"; no content at all.
  e.input = support::Json::object{
      {"summary", support::Json(tc.name + " completed")}};
  e.decision = "allow";
  core::journal::append(p->cwd, p->sessionId, e);
  cc::emit("{}");
  return 0;
}

// Stop: turn boundary. Demotes turn-tier capabilities, persists, and records
// one turn event (async hook).
int stop() {
  std::string err;
  auto p = cc::parsePayload(readStdin(), err);
  if (!p) cc::failNonBlocking("warden stop: payload parse failed: " + err);
  if (!validSessionId(p->sessionId))
    cc::failNonBlocking("warden stop: illegal session_id: " + p->sessionId);

  auto stOpt = core::state::load(p->cwd, p->sessionId);
  if (!stOpt) cc::failNonBlocking("warden stop: state corrupt");
  core::attr::endTurn(*stOpt);
  core::state::save(p->cwd, p->sessionId, *stOpt);

  core::journal::Entry e;
  e.t = nowIso();
  e.ev = "turn";             // turn-boundary event
  e.scopes = scopeNames(*stOpt);
  e.caps = core::attr::activeCaps(*stOpt);
  e.input = support::Json(nullptr);
  e.decision = "allow";
  core::journal::append(p->cwd, p->sessionId, e);
  cc::emit("{}");
  return 0;
}

}  // namespace warden
