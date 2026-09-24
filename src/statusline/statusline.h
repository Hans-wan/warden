// Single-line statusline renderer. Pure functions over the session state and
// policy; the CLI wrapper (main.cpp) owns stdin parsing and file loading.
// Contract: the output is one line, never empty, never an error — a statusline
// that flashes red or dies is worse than a dim one.
#pragma once
#include <string>
#include <string_view>

#include "core/policy/policy.h"
#include "core/state/state.h"

namespace core::statusline {

// Mode word for the current policy default.
std::string_view modeWord(core::policy::Mode m);

// True only when noColorEnv is nullptr, i.e. NO_COLOR is absent from the
// environment. Per the controller ruling, NO_COLOR "set" disables color
// regardless of value — even an empty string counts as set.
bool detectColor(const char* noColorEnv);

// Renders the single statusline line (no trailing newline). st == nullptr or
// pol == nullptr degrades to "warden: observing" (dim when color). color
// should come from detectColor(); NO_COLOR handling lives there, not here.
std::string renderOneLine(const core::state::State* st,
                          const core::policy::Policy* pol, bool color);

// Multi-line human report for the /warden:status slash command. journalPath
// is printed as-is in the JOURNAL section; pass "" when unknown. Never empty,
// never an error — same degradation contract as renderOneLine. color comes
// from detectColor(); NO_COLOR handling lives there, not here.
std::string renderReport(const core::state::State& st,
                         const core::policy::Policy& pol,
                         const std::string& journalPath, bool color);

}  // namespace core::statusline
