// hooks.json generation (shipped with the plugin; zero user configuration).
#pragma once
#include <string>

namespace cc {

std::string hooksJson(const std::string& binPath);

// Synthesizes a harmless PreToolUse payload (doctor's self-check probe).
// Assembling host-specific field names is the adapter's job, so this lives
// here rather than in doctor.
std::string probePreToolPayload(const std::string& cwd, const std::string& sessionId);

}  // namespace cc
