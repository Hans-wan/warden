#include "core/capability/besteffort.h"

#include <cstdint>
#include <regex>
#include <string>
#include <unordered_set>

namespace core::cap {

// One tag per rule. C++20 has no compile-time regex, so compile at construction
// and keep one-time singletons.
// Note: regexes are bypassable via bash -c / base64 / python -c — this is
// observation, not a security boundary.
namespace {

struct Rule {
  Tag tag;
  std::regex re;
};

const std::vector<Rule>& rules() {
  static const std::vector<Rule> kRules = {
      {Tag::NetListen,
       std::regex(R"((^|\s)(-l\b|--listen\b|--port[= ]|\bserve\b|uvicorn|gunicorn|flask run))")},
      {Tag::PkgInstall,
       std::regex(R"(^(npm|pnpm|yarn)\b.*\b(install|i|add|update)\b|^pip3? install\b|^cargo (add|install)\b|^brew install\b|^go install\b)")},
      {Tag::EnvRead, std::regex(R"(^(env|printenv)\b)")},
      {Tag::SecretsRead,
       std::regex(R"(\b(cat|less|more|head|tail)\b.*(\.env|id_rsa|id_ed25519|credentials|\.netrc))")},
      {Tag::SysModify, std::regex(R"(^(sudo|chmod|chown|chgrp|chattr)\b|^tee /etc/|^> ?/etc/)")},
      {Tag::ProcKill, std::regex(R"(^(kill|pkill|killall|fuser)\b)")},
      {Tag::Container, std::regex(R"(^(docker|podman|kubectl|nerdctl)\b)")},
      {Tag::DBWrite,
       std::regex(R"(\b(insert into|update .* set|delete from|drop table|alter table|truncate)\b)")},
      {Tag::CloudWrite,
       std::regex(R"(^aws s3 (rm|cp|mv|sync)\b|^gcloud (storage|compute|deploy)\b|^az (storage|vm|webapp)\b)")},
  };
  return kRules;
}

}  // namespace

std::vector<Tag> bestEffortExtra(std::string_view command) {
  std::string cmd(command);
  if (cmd.empty()) return {};
  std::vector<Tag> out;
  std::unordered_set<uint8_t> seen;
  for (const auto& r : rules()) {
    if (std::regex_search(cmd, r.re)) {
      auto u = static_cast<uint8_t>(r.tag);
      if (seen.insert(u).second) out.push_back(r.tag);
    }
  }
  return out;
}

}  // namespace core::cap
