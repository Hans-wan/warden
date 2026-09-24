#include "core/capability/tags.h"

#include <array>

namespace core::cap {
namespace {
constexpr std::array<Tag, 19> kAll{
    Tag::FsRead, Tag::FsWrite, Tag::FsDelete, Tag::Shell,
    Tag::GitRead, Tag::GitWrite, Tag::GitPush,
    Tag::NetFetch, Tag::AgentSpawn, Tag::MCPCall,
    Tag::NetListen, Tag::PkgInstall, Tag::EnvRead, Tag::SecretsRead,
    Tag::SysModify, Tag::ProcKill, Tag::Container, Tag::DBWrite,
    Tag::CloudWrite,
};
constexpr std::array<std::string_view, 19> kNames{
    "fs:read", "fs:write", "fs:delete", "shell",
    "git:read", "git:write", "git:push",
    "net:fetch", "agent:spawn", "mcp:call",
    "net:listen", "pkg:install", "env:read", "secrets:read", "sys:modify",
    "proc:kill", "container", "db:write", "cloud:write",
};
}  // namespace

const std::vector<Tag>& all() {
  static const std::vector<Tag> v(kAll.begin(), kAll.end());
  return v;
}

// Only the three read-only tags persist across turns; secrets:read reads but stays turn-tier.
Tier tierOf(Tag t) {
  switch (t) {
    case Tag::FsRead:
    case Tag::GitRead:
    case Tag::EnvRead:
      return Tier::Persistent;
    default:
      return Tier::Turn;
  }
}

bool isBestEffort(Tag t) {
  return static_cast<std::uint8_t>(t) >= 10;  // first 10 are the core set
}

std::string_view name(Tag t) { return kNames[static_cast<std::uint8_t>(t)]; }

std::optional<Tag> fromName(std::string_view n) {
  for (std::size_t i = 0; i < kNames.size(); ++i)
    if (kNames[i] == n) return kAll[i];
  return std::nullopt;
}

}  // namespace core::cap
