// Multi-line status report: the human-readable sibling of renderOneLine.
// Same state, same policy, but structured for reading rather than glancing.
#include <sstream>
#include <string>
#include <string_view>

#include "core/journal/journal.h"
#include "statusline/statusline.h"

namespace core::statusline {
namespace {

std::string capsList(const core::state::Scope& sc) {
  std::string out;
  for (std::size_t i = 0; i < sc.caps.size(); ++i) {
    if (i) out += ", ";
    out += sc.caps[i];
  }
  return out;
}

// Replaces every control byte with a space — the same formatting-robustness
// rule renderOneLine applies to its host-supplied segments (skill names, tool
// names, denial summaries). The report is multi-line by design, so an embedded
// newline is not merely cosmetic here: a hostile skill name, session id, or
// denied-command summary could forge a section heading or inject a terminal
// escape into output the user is reading. Formatting robustness, not
// redaction — the byte stays, only its control meaning is dropped. Every
// host/model-supplied string here gets the pass: the denial summary is only
// "redacted" for privacy (content hashes, no file bodies), not for control
// safety — sanitizedInput keeps a Bash command verbatim by design
// (core/journal/privacy.cpp) and denySummary copies that raw text with only
// byte truncation, so newlines and escapes survive into it. caps and the
// journal path are warden-generated or caller-supplied.
std::string sanitizeControls(std::string_view s) {
  std::string out(s);
  for (char& c : out) {
    const unsigned char b = static_cast<unsigned char>(c);
    if (b < 0x20 || b == 0x7f) c = ' ';
  }
  return out;
}

}  // namespace

std::string renderReport(const core::state::State& st,
                         const core::policy::Policy& pol,
                         const std::string& journalPath, bool color) {
  // The report body is plain text; the host already styles the slash-command
  // output it wraps. The flag stays in the signature so callers pass
  // detectColor() the same way they do for renderOneLine, and so a future
  // colored variant can opt in without a call-site change.
  (void)color;
  std::ostringstream o;
  o << "warden status — session " << sanitizeControls(st.sessionId) << "\n";

  o << "\nMODE\n  " << modeWord(pol.def)
    << (pol.def == core::policy::Mode::Observe
            ? "  (records only, never blocks)"
            : pol.def == core::policy::Mode::Ask
                  ? "  (violations come to you as permission prompts)"
                  : "  (violations are hard-blocked)")
    << "\n";

  o << "\nSCOPES\n";
  if (st.scopes.empty()) {
    o << "  no skill scopes open — base policy applies\n";
  } else {
    for (const auto& sc : st.scopes) {
      o << "  " << sanitizeControls(sc.skill) << "  [" << capsList(sc) << "]  ("
        << (sc.persistSession ? "capabilities last for the session"
                              : "capabilities expire at turn end")
        << ")\n";
    }
  }

  o << "\nCOUNTERS\n"
    << "  allowed: " << st.counters.allow << "\n"
    << "  asked:   " << st.counters.ask << "\n"
    << "  denied:  " << st.counters.deny << "\n";

  o << "\nJOURNAL\n  " << (journalPath.empty() ? "(not found)" : journalPath) << "\n";

  if (st.lastDenial) {
    o << "\nLAST DENIAL\n"
      << "  " << sanitizeControls(st.lastDenial->tool) << ": "
      << sanitizeControls(st.lastDenial->summary) << "\n"
      << "  missing capabilities: " << st.lastDenial->caps << "\n"
      << "  at: " << st.lastDenial->t << "\n";
  } else {
    // No denial yet: the section header is omitted entirely and the reader
    // gets a plain reassurance instead of an empty section.
    o << "\n  no blocks this session\n";
  }
  return o.str();
}

}  // namespace core::statusline
