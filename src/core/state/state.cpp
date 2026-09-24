#include "core/state/state.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace core::state {
namespace fs = std::filesystem;

std::string dir(std::string_view root) {
  return (fs::path(root) / ".warden" / "state").string();
}

namespace {
// sessionId path validation (final safety net). Private copy of the same helper
// in journal.cpp: sharing it would need a new header and a cross-module
// dependency; two ~8-line copies are simpler. Illegal sids never reach path
// assembly.
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

fs::path fileFor(std::string_view root, std::string_view sessionId) {
  return fs::path(dir(root)) / (std::string(sessionId) + ".json");
}
}  // namespace

support::Json toJson(const State& s) {
  auto scopes = support::Json::array();
  for (auto& sc : s.scopes) {
    auto caps = support::Json::array();
    for (auto& c : sc.caps) caps.push_back(support::Json(c));
    scopes.push_back(support::Json::object{
        {"skill", support::Json(sc.skill)},
        {"caps", caps},
        {"persist_session", support::Json(sc.persistSession)},
    });
  }
  support::Json obj = support::Json::object{
      {"session_id", support::Json(s.sessionId)},
      {"scopes", scopes},
  };
  support::Json counters = support::Json::object{
      {"allow", support::Json(static_cast<double>(s.counters.allow))},
      {"ask", support::Json(static_cast<double>(s.counters.ask))},
      {"deny", support::Json(static_cast<double>(s.counters.deny))},
  };
  obj["counters"] = std::move(counters);
  if (s.lastDenial) {
    obj["last_denial"] = support::Json::object{
        {"tool", support::Json(s.lastDenial->tool)},
        {"summary", support::Json(s.lastDenial->summary)},
        {"caps", support::Json(s.lastDenial->caps)},
        {"t", support::Json(s.lastDenial->t)},
    };
  }
  return obj;
}

std::optional<State> fromJson(const support::Json& j, std::string& err) {
  // Note: json.h's find returns a std::map
  // iterator, so values come via it->second; iterate arrays with
  // size() + operator[](size_t) subscripting.
  State out;
  if (!j.is_object()) {
    err = "not an object";
    return std::nullopt;
  }
  auto sid = j.find("session_id");
  if (sid != j.end() && sid->second.is_string())
    out.sessionId = sid->second.get<std::string>();

  auto scopes = j.find("scopes");
  if (scopes != j.end() && scopes->second.is_array()) {
    const support::Json& scopesArr = scopes->second;
    for (std::size_t i = 0; i < scopesArr.size(); ++i) {
      const support::Json& sc = scopesArr[i];
      Scope s;
      auto sk = sc.find("skill");
      if (sk != sc.end() && sk->second.is_string())
        s.skill = sk->second.get<std::string>();
      auto caps = sc.find("caps");
      if (caps != sc.end() && caps->second.is_array()) {
        const support::Json& capsArr = caps->second;
        for (std::size_t k = 0; k < capsArr.size(); ++k)
          if (capsArr[k].is_string())
            s.caps.push_back(capsArr[k].get<std::string>());
      }
      auto p = sc.find("persist_session");
      if (p != sc.end() && p->second.is_bool())
        s.persistSession = p->second.get<bool>();
      out.scopes.push_back(std::move(s));
    }
  }

  auto counters = j.find("counters");
  if (counters != j.end() && counters->second.is_object()) {
    const support::Json& cj = counters->second;
    auto a = cj.find("allow");
    if (a != cj.end() && a->second.is_number())
      out.counters.allow = static_cast<std::uint64_t>(a->second.get<double>());
    auto q = cj.find("ask");
    if (q != cj.end() && q->second.is_number())
      out.counters.ask = static_cast<std::uint64_t>(q->second.get<double>());
    auto d = cj.find("deny");
    if (d != cj.end() && d->second.is_number())
      out.counters.deny = static_cast<std::uint64_t>(d->second.get<double>());
  }
  auto ld = j.find("last_denial");
  if (ld != j.end() && ld->second.is_object()) {
    const support::Json& dj = ld->second;
    LastDenial ld2;
    auto t1 = dj.find("tool");
    if (t1 != dj.end() && t1->second.is_string()) ld2.tool = t1->second.get<std::string>();
    auto t2 = dj.find("summary");
    if (t2 != dj.end() && t2->second.is_string()) ld2.summary = t2->second.get<std::string>();
    auto t3 = dj.find("caps");
    if (t3 != dj.end() && t3->second.is_string()) ld2.caps = t3->second.get<std::string>();
    auto t4 = dj.find("t");
    if (t4 != dj.end() && t4->second.is_string()) ld2.t = t4->second.get<std::string>();
    out.lastDenial = std::move(ld2);
  }
  return out;
}

std::optional<State> load(std::string_view root, std::string_view sessionId) {
  if (!validSessionId(sessionId)) return State{};  // illegal sid -> zero state (fail-safe)
  std::ifstream in(fileFor(root, sessionId));
  if (!in) return State{};  // missing -> zero state
  std::string buf((std::istreambuf_iterator<char>(in)),
                  std::istreambuf_iterator<char>());
  std::string err;
  auto j = support::Json::parse(buf, err);
  if (!err.empty()) return std::nullopt;
  return fromJson(j, err);
}

bool save(std::string_view root, std::string_view sessionId, const State& s) {
  if (!validSessionId(sessionId)) return false;  // defense in depth (primary check lives in hooks_cmd)
  std::error_code ec;
  fs::create_directories(dir(root), ec);
  if (ec) return false;

  auto data = toJson(s).dump();
  auto tmp = fileFor(root, sessionId);
  tmp += ".tmp";
  {
    std::ofstream out(tmp, std::ios::trunc);
    if (!out) return false;
    out << data;
  }
  fs::rename(tmp, fileFor(root, sessionId), ec);
  if (ec) {
    fs::remove(tmp, ec);
    return false;
  }
  return true;
}

}  // namespace core::state
