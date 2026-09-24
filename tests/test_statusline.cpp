#include <gtest/gtest.h>

#include "core/policy/policy.h"
#include "core/state/state.h"
#include "statusline/statusline.h"

using core::policy::Mode;
using core::policy::Policy;
using core::state::LastDenial;
using core::state::Scope;
using core::state::State;

static Policy pol(Mode m) {
  Policy p;
  p.def = m;
  return p;
}

TEST(Statusline, NullStateRendersObserving) {
  auto out = core::statusline::renderOneLine(nullptr, nullptr, false);
  EXPECT_EQ(out, "warden: observing");
}

TEST(Statusline, PlainModeAndCountersNoColor) {
  State s;
  s.sessionId = "s";
  s.counters.allow = 87;
  s.counters.deny = 2;
  Policy p = pol(Mode::Observe);
  auto out = core::statusline::renderOneLine(&s, &p, false);
  // mode glyph + mode + counter trio; no scopes -> no skill segment
  EXPECT_NE(out.find("OBSERVE"), std::string::npos);
  EXPECT_NE(out.find("87"), std::string::npos);
  EXPECT_NE(out.find("2"), std::string::npos);
  EXPECT_EQ(out.find("\033["), std::string::npos);  // no ANSI when color=false
}

TEST(Statusline, ModeGlyphsPerTier) {
  State s;
  s.sessionId = "s";
  for (auto m : {Mode::Observe, Mode::Ask, Mode::Enforce}) {
    Policy p = pol(m);
    auto out = core::statusline::renderOneLine(&s, &p, false);
    EXPECT_NE(out.find(m == Mode::Observe ? "OBSERVE"
                        : m == Mode::Ask   ? "ASK"
                                           : "ENFORCE"),
              std::string::npos);
  }
}

TEST(Statusline, ScopesShownFirstPlusN) {
  State s;
  s.sessionId = "s";
  s.scopes = {Scope{"pdf-tools", {"fs:read", "shell"}, false},
              Scope{"writer", {"fs:write"}, true}};
  Policy p = pol(Mode::Enforce);
  auto out = core::statusline::renderOneLine(&s, &p, false);
  EXPECT_NE(out.find("pdf-tools"), std::string::npos);
  EXPECT_NE(out.find("+1"), std::string::npos);
}

TEST(Statusline, LastDenialShownTruncated) {
  State s;
  s.sessionId = "s";
  std::string longCmd(80, 'x');
  s.lastDenial = LastDenial{"bash", longCmd, "fs:delete", "t"};
  Policy p = pol(Mode::Enforce);
  auto out = core::statusline::renderOneLine(&s, &p, false);
  EXPECT_NE(out.find("bash"), std::string::npos);
  EXPECT_LE(out.size(), 200u);  // truncation keeps the line bounded
}

TEST(Statusline, FalseColorArgumentDisablesAnsi) {
  State s;
  s.sessionId = "s";
  Policy p = pol(Mode::Enforce);
  auto out = core::statusline::renderOneLine(&s, &p, false);
  EXPECT_EQ(out.find("\033["), std::string::npos);
}

TEST(Statusline, DetectColorTreatsPresenceAsNoColor) {
  EXPECT_TRUE(core::statusline::detectColor(nullptr));   // NO_COLOR absent
  EXPECT_FALSE(core::statusline::detectColor(""));       // present, empty
  EXPECT_FALSE(core::statusline::detectColor("1"));      // present, any value
}

TEST(Statusline, ControlCharsInDenialAreSanitized) {
  State s;
  s.sessionId = "s";
  // A denied heredoc command: embedded newline plus an ANSI color escape.
  s.lastDenial = LastDenial{"bash", "line1\nline2\033[31m", "fs:delete", "t"};
  Policy p = pol(Mode::Enforce);
  auto out = core::statusline::renderOneLine(&s, &p, false);
  EXPECT_EQ(out.find('\n'), std::string::npos);   // stays one line
  EXPECT_EQ(out.find('\r'), std::string::npos);
  EXPECT_EQ(out.find("\033["), std::string::npos);  // no injected color
  EXPECT_NE(out.find("bash"), std::string::npos);
}

TEST(Statusline, ControlCharsInSkillNameAreSanitized) {
  State s;
  s.sessionId = "s";
  s.scopes = {Scope{"pdf\ntools\033[31m", {"fs:read"}, false}};
  Policy p = pol(Mode::Enforce);
  auto out = core::statusline::renderOneLine(&s, &p, false);
  EXPECT_EQ(out.find('\n'), std::string::npos);
  EXPECT_EQ(out.find("\033["), std::string::npos);
}

TEST(StatusReport, HasAllSectionsWhenDenialPresent) {
  State s;
  s.sessionId = "s1";
  s.scopes = {Scope{"pdf-tools", {"fs:read", "shell"}, false}};
  s.counters.allow = 3;
  s.counters.deny = 1;
  s.lastDenial = LastDenial{"bash", "rm -rf tmp/", "fs:delete", "2026-09-23T10:00:00Z"};
  auto out = core::statusline::renderReport(s, pol(Mode::Enforce), "/tmp/j/s.json", false);
  EXPECT_NE(out.find("MODE"), std::string::npos);
  EXPECT_NE(out.find("ENFORCE"), std::string::npos);
  EXPECT_NE(out.find("SCOPES"), std::string::npos);
  EXPECT_NE(out.find("pdf-tools"), std::string::npos);
  EXPECT_NE(out.find("fs:read"), std::string::npos);
  EXPECT_NE(out.find("JOURNAL"), std::string::npos);
  EXPECT_NE(out.find("COUNTERS"), std::string::npos);
  EXPECT_NE(out.find("allowed: 3"), std::string::npos);
  EXPECT_NE(out.find("denied:  1"), std::string::npos);
  EXPECT_NE(out.find("LAST DENIAL"), std::string::npos);
  EXPECT_EQ(out.find("\033["), std::string::npos);
}

TEST(StatusReport, OmitsLastDenialWhenAbsent) {
  State s;
  s.sessionId = "s";
  auto out = core::statusline::renderReport(s, pol(Mode::Observe), "/tmp/j", false);
  EXPECT_EQ(out.find("LAST DENIAL"), std::string::npos);
  EXPECT_NE(out.find("no blocks this session"), std::string::npos);
}

TEST(StatusReport, TurnAndSessionTiersLabeled) {
  State s;
  s.sessionId = "s";
  s.scopes = {Scope{"pdf-tools", {"fs:read"}, false}, Scope{"writer", {"fs:write"}, true}};
  auto out = core::statusline::renderReport(s, pol(Mode::Observe), "/j", false);
  // Each annotation string is unique to its tier, so a whole-report search
  // still pins the behavior: a deleted tier branch drops its phrase. (A bare
  // find("session") would pass on the header line alone.)
  EXPECT_NE(out.find("capabilities expire at turn end"), std::string::npos);
  EXPECT_NE(out.find("capabilities last for the session"), std::string::npos);
}

TEST(StatusReport, ControlCharsInSkillToolAndSessionIdAreSanitized) {
  State s;
  s.sessionId = "sid\nforged\033[31m";  // newline + ANSI in the session id
  s.scopes = {Scope{"pdf\ntools\033[31m", {"fs:read"}, false}};
  // The denial summary is host-supplied text too. sanitizedInput keeps a Bash
  // command verbatim by design (privacy.cpp), so a denied command reaches this
  // string with its newlines and escapes intact and needs the same pass. The
  // summary carries a green escape (32m) so a hit below pins THIS field, not
  // the tool/session id segments (which use 31m).
  s.lastDenial = LastDenial{"ba\nsh\033[31m", "rm -rf\ntmp/\033[32m", "fs:delete", "t"};
  auto out = core::statusline::renderReport(s, pol(Mode::Enforce), "/j", false);
  EXPECT_EQ(out.find('\033'), std::string::npos);  // no injected escape
  EXPECT_EQ(out.find("\033[32m"), std::string::npos);  // ...including from the summary
  // The injected newline must not start a line: a hostile name cannot forge a
  // section heading or a first column of its own.
  EXPECT_EQ(out.find("\nforged"), std::string::npos);
  EXPECT_EQ(out.find("rm -rf\ntmp/"), std::string::npos);  // ...nor from the summary
  EXPECT_NE(out.find("sid forged"), std::string::npos);  // byte kept, control dropped
  EXPECT_NE(out.find("pdf tools"), std::string::npos);
  EXPECT_NE(out.find("ba sh"), std::string::npos);
  EXPECT_NE(out.find("rm -rf tmp/"), std::string::npos);
}
