// Capability tags and lifecycle tiering.
// Judgment helpers stay pure: no I/O.
#pragma once
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace core::cap {

enum class Tag : std::uint8_t {
  // Core set: detected precisely in v1 (structured fields or reliable parsing).
  FsRead, FsWrite, FsDelete, Shell,
  GitRead, GitWrite, GitPush,
  NetFetch, AgentSpawn, MCPCall,
  // Best-effort set: regex-matched, guaranteed incomplete. Docs must say best-effort.
  NetListen, PkgInstall, EnvRead, SecretsRead, SysModify,
  ProcKill, Container, DBWrite, CloudWrite,
};

enum class Tier : std::uint8_t { Persistent = 0, Turn = 1 };

// 10 core + 9 best-effort.
const std::vector<Tag>& all();
Tier tierOf(Tag t);
bool isBestEffort(Tag t);
// String form, e.g. "fs:read". Shared by journal persistence and policy files.
std::string_view name(Tag t);
// Reverse lookup; unknown names yield nullopt (policy-file tolerance).
std::optional<Tag> fromName(std::string_view n);

}  // namespace core::cap
