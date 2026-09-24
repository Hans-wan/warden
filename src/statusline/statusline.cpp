#include "statusline/statusline.h"

#include <string>
#include <vector>

namespace core::statusline {
namespace {

constexpr const char* kReset = "\033[0m";
constexpr const char* kDim = "\033[2m";
constexpr const char* kYellow = "\033[33m";
constexpr const char* kRed = "\033[31m";

std::string paint(bool color, const char* code, std::string_view s) {
  if (!color) return std::string(s);
  return std::string(code) + std::string(s) + kReset;
}

const char* modeColor(core::policy::Mode m) {
  switch (m) {
    case core::policy::Mode::Enforce: return kRed;
    case core::policy::Mode::Ask: return kYellow;
    case core::policy::Mode::Observe: return kDim;
  }
  return kDim;
}

// Replaces every control byte with a space. The statusline contract is a single
// line with no escape injection: a denied shell command routinely carries
// newlines (heredocs) and may embed ANSI escapes, either of which would break
// the layout or inject color even when color is off. Formatting robustness, not
// redaction — the byte stays, only its control meaning is dropped. Applied to
// every segment that carries host-supplied text (skill names, tool/summary).
std::string sanitizeControls(std::string_view s) {
  std::string out(s);
  for (char& c : out) {
    const unsigned char b = static_cast<unsigned char>(c);
    if (b < 0x20 || b == 0x7f) c = ' ';
  }
  return out;
}

// Sanitizes, then truncates to maxChars with an ellipsis; ASCII-safe
// (statusline segments are user-supplied paths/commands — bytes, not code
// points, are counted, which only ever risks splitting a multibyte char on
// pathological input).
std::string ellipsize(std::string_view s, std::size_t maxChars) {
  if (maxChars == 0) return "";
  std::string clean = sanitizeControls(s);
  if (clean.size() <= maxChars) return clean;
  return clean.substr(0, maxChars - 1) + "…";
}

}  // namespace

std::string_view modeWord(core::policy::Mode m) {
  switch (m) {
    case core::policy::Mode::Observe: return "OBSERVE";
    case core::policy::Mode::Ask: return "ASK";
    case core::policy::Mode::Enforce: return "ENFORCE";
  }
  return "OBSERVE";
}

bool detectColor(const char* noColorEnv) {
  return noColorEnv == nullptr;
}

std::string renderOneLine(const core::state::State* st,
                          const core::policy::Policy* pol, bool color) {
  if (st == nullptr || pol == nullptr)
    return paint(color, kDim, "warden: observing");

  std::vector<std::string> segs;
  segs.push_back(paint(color, modeColor(pol->def),
                       "● " + std::string(modeWord(pol->def))));

  // Segment 2: open skills — first + "+N". The skill name is sanitized and
  // ellipsized before the "+N" suffix is appended, so the count always survives.
  if (!st->scopes.empty()) {
    std::string s = ellipsize(st->scopes.front().skill, 32);
    if (st->scopes.size() > 1)
      s += "+" + std::to_string(st->scopes.size() - 1);
    segs.push_back(paint(color, kDim, s));
  }

  // Segment 3: counters, always all three so column widths stay stable.
  segs.push_back(paint(color, modeColor(pol->def),
                       "⛔" + std::to_string(st->counters.deny) + " " +
                           "❓" + std::to_string(st->counters.ask) + " " +
                           "✅" + std::to_string(st->counters.allow)));

  // Segment 4: last denial, only when one exists.
  if (st->lastDenial) {
    segs.push_back(paint(color, kRed,
                         "⛔ " + ellipsize(st->lastDenial->tool + ": " +
                                               st->lastDenial->summary,
                                           40)));
  }

  std::string out;
  for (std::size_t i = 0; i < segs.size(); ++i) {
    if (i) out += " │ ";
    out += segs[i];
  }
  return out;
}
}  // namespace core::statusline
