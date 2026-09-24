// Trajectory jsonl append and read. Pure file I/O: no exceptions, failures
// surface through return values.
#include "core/journal/journal.h"

#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;

namespace core::journal {

std::string dir(std::string_view root) {
  return (fs::path(root) / ".warden" / "journal").string();
}

namespace {

// sessionId path validation (final safety net). sessionId comes from the host
// payload; splicing it straight into a file path lets "../../evil" escape
// .warden/journal/. Only [A-Za-z0-9._-] is allowed, and "." / ".." are
// excluded outright. state.cpp holds a private copy with identical semantics
// (sharing would need a new header and a cross-module dependency — not worth it).
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

std::string fileFor(std::string_view root, std::string_view sessionId) {
  return (fs::path(dir(root)) / (std::string(sessionId) + ".jsonl")).string();
}

// Entry -> JSON object. Field names are the read-back contract for replay

support::Json toJson(const Entry& e) {
  support::Json::array scopes;
  for (const std::string& s : e.scopes) scopes.push_back(support::Json(s));
  support::Json::array caps;
  for (const std::string& c : e.caps) caps.push_back(support::Json(c));
  return support::Json::object{
      {"t", support::Json(e.t)},
      {"ev", support::Json(e.ev)},
      {"tool", support::Json(e.tool)},
      {"scope", support::Json(std::move(scopes))},
      {"cap", support::Json(std::move(caps))},
      {"input", e.input},
      {"decision", support::Json(e.decision)},
      {"dur_ms", support::Json(e.durMs)}};
}

std::vector<std::string> stringArray(const support::Json& v) {
  std::vector<std::string> out;
  for (std::size_t i = 0; i < v.size(); ++i) {
    const support::Json& item = v[i];
    if (item.is_string()) out.push_back(item.get<std::string>());
  }
  return out;
}

// JSON object -> Entry. Missing fields fall back to defaults.
Entry fromJson(const support::Json& j) {
  Entry e;
  e.t = j.get_string("t");
  e.ev = j.get_string("ev");
  e.tool = j.get_string("tool");
  e.scopes = stringArray(j.at("scope"));
  e.caps = stringArray(j.at("cap"));
  e.input = j.at("input");
  e.decision = j.get_string("decision");
  e.durMs = j.get_number("dur_ms");
  return e;
}

}  // namespace

bool append(std::string_view root, std::string_view sessionId, const Entry& e) {
  if (!validSessionId(sessionId)) return false;  // defense in depth (primary check lives in hooks_cmd)
  const std::string d = dir(root);
  std::error_code ec;
  fs::create_directories(d, ec);
  if (ec) return false;

  std::ofstream out(fileFor(root, sessionId), std::ios::app | std::ios::binary);
  if (!out.is_open()) return false;
  out << toJson(e).dump() << "\n";
  out.flush();
  return out.good();
}

std::vector<Entry> readAll(std::string_view root, std::string_view sessionId) {
  std::vector<Entry> out;
  if (!validSessionId(sessionId)) return out;  // illegal sid: no trajectory to read
  std::ifstream in(fileFor(root, sessionId), std::ios::binary);
  if (!in.is_open()) return out;  // a missing file is not an error.

  std::string line;
  while (std::getline(in, line)) {
    if (line.empty()) continue;
    std::string err;
    support::Json j = support::Json::parse(line, err);
    // Skip bad lines and keep going: the journal must never refuse the rest of
    // a trajectory because of one bad record.
    if (!err.empty() || !j.is_object()) continue;
    out.push_back(fromJson(j));
  }
  return out;
}

}  // namespace core::journal
