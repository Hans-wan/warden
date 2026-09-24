#include "core/capability/bash.h"

#include <algorithm>
#include <cstddef>
#include <string>
#include <unordered_set>

namespace core::cap {
namespace {

// Split a compound command linearly on ; && || | &. Separators inside quotes
// don't split, and neither do operators inside $()/` (inner text is extracted
// separately by commandSubs and scanned recursively).
std::vector<std::string> splitSegments(std::string_view cmd) {
  std::vector<std::string> out;
  std::string cur;
  char quote = 0;
  int substDepth = 0;  // open $( nesting depth; backticks tracked by their own flag
  bool inBacktick = false;
  for (std::size_t i = 0; i < cmd.size(); ++i) {
    char c = cmd[i];
    if (quote) {
      cur += c;
      if (c == quote) quote = 0;
      continue;
    }
    if (c == '\'' || c == '"') {
      quote = c;
      cur += c;
      continue;
    }
    if (c == '`') {
      inBacktick = !inBacktick;
      cur += c;
      continue;
    }
    if (inBacktick) {
      cur += c;
      continue;
    }
    if (c == '$' && i + 1 < cmd.size() && cmd[i + 1] == '(') {
      ++substDepth;
      cur += c;
      continue;
    }
    if (c == '(' && substDepth > 0) {
      ++substDepth;  // subshell parens count too, so the substitution doesn't close early
      cur += c;
      continue;
    }
    if (c == ')' && substDepth > 0) {
      --substDepth;
      cur += c;
      continue;
    }
    if (c == '\\' && i + 1 < cmd.size()) {
      cur += c;
      cur += cmd[i + 1];
      ++i;
      continue;
    }
    if (substDepth > 0) {
      // Inside $() we don't split on operators; commandSubs extracts the whole thing.
      cur += c;
      continue;
    }
    if (c == ';' || c == '|' || c == '&') {
      // && and || are two characters: skip the second
      if (i + 1 < cmd.size() && cmd[i + 1] == c) ++i;
      out.push_back(cur);
      cur.clear();
      continue;
    }
    cur += c;
  }
  out.push_back(cur);
  return out;
}

// Extract the inner text of every $() and ` (quote-aware; one nesting level is
// handled by recursing back into fromBash).
std::vector<std::string> commandSubs(std::string_view cmd) {
  std::vector<std::string> out;
  char quote = 0;
  for (std::size_t i = 0; i < cmd.size(); ++i) {
    char c = cmd[i];
    if (quote) {
      if (c == quote) quote = 0;
      continue;
    }
    if (c == '\'' || c == '"') {
      quote = c;
      continue;
    }
    if (c == '`') {
      std::size_t end = cmd.find('`', i + 1);
      if (end == std::string_view::npos) break;  // unclosed: drop
      out.emplace_back(cmd.substr(i + 1, end - i - 1));
      i = end;
      continue;
    }
    if (c == '$' && i + 1 < cmd.size() && cmd[i + 1] == '(') {
      int depth = 1;
      std::size_t j = i + 2;
      for (; j < cmd.size() && depth > 0; ++j) {
        char d = cmd[j];
        if (d == '(') ++depth;
        else if (d == ')') --depth;
      }
      if (depth > 0) break;  // unclosed: drop
      out.emplace_back(cmd.substr(i + 2, j - i - 3));
      i = j - 1;
    }
  }
  return out;
}

// Quote-aware whitespace tokenization; unclosed quotes are tolerated, not errors.
std::vector<std::string> tokenize(std::string_view seg) {
  std::vector<std::string> toks;
  std::string cur;
  char quote = 0;
  bool inTok = false;
  for (char c : seg) {
    if (quote) {
      if (c == quote) quote = 0;
      else cur += c;
      inTok = true;
      continue;
    }
    if (c == '\'' || c == '"') {
      quote = c;
      inTok = true;
      continue;
    }
    if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
      if (inTok) {
        toks.push_back(cur);
        cur.clear();
        inTok = false;
      }
      continue;
    }
    cur += c;
    inTok = true;
  }
  if (inTok) toks.push_back(cur);
  return toks;
}

// git subcommand -> read/write/push. Skips leading options.
std::vector<Tag> classifyGit(const std::vector<std::string>& args) {
  std::size_t i = 0;
  while (i < args.size() && !args[i].empty() && args[i][0] == '-') ++i;
  if (i >= args.size()) return {Tag::GitRead};

  const std::string& sub = args[i];
  if (sub == "push") return {Tag::GitPush};
  static const std::unordered_set<std::string> kWrite{
      "commit", "merge", "rebase", "reset", "revert", "cherry-pick",
      "add", "rm", "mv", "checkout", "switch", "restore", "tag",
      "stash", "clean", "init", "clone", "fetch", "pull"};
  if (kWrite.count(sub)) return {Tag::GitWrite};
  return {Tag::GitRead};
}

// In-place edit tools with -i variants (-i, -i.bak, -in, ...) -> write.
bool isInPlaceEditor(const std::string& cmd, const std::vector<std::string>& args) {
  if (cmd != "sed" && cmd != "perl" && cmd != "awk") return false;
  for (const std::string& a : args)
    if (a.size() >= 2 && a[0] == '-' && a.find('i') != std::string::npos) return true;
  return false;
}

// One segment -> tags. toks is non-empty.
std::vector<Tag> classifySegment(const std::vector<std::string>& toks) {
  // Skip leading environment assignments: FOO=bar cmd
  std::size_t i = 0;
  while (i < toks.size() && toks[i].find('=') != std::string::npos &&
         !toks[i].empty() && toks[i][0] != '-')
    ++i;
  if (i >= toks.size()) return {};

  std::string cmd = toks[i];
  std::vector<std::string> args(toks.begin() + i + 1, toks.end());
  // Strip path prefixes: /usr/bin/git -> git
  auto slash = cmd.rfind('/');
  if (slash != std::string::npos) cmd = cmd.substr(slash + 1);

  // sudo recursion: strip sudo, re-classify the inner command, add sys:modify
  if (cmd == "sudo" || cmd == "doas") {
    std::vector<Tag> tags{Tag::SysModify};
    if (!args.empty()) {
      // Skip options like -u user; everything from the first non-option token
      // on is the inner command with its arguments.
      std::size_t j = 0;
      while (j < args.size() && !args[j].empty() && args[j][0] == '-') {
        if (j + 1 < args.size() && args[j] == "-u") j += 2;
        else ++j;
      }
      if (j < args.size())
        for (Tag t : classifySegment({args.begin() + j, args.end()})) tags.push_back(t);
    }
    return tags;
  }

  if (cmd == "git") return classifyGit(args);
  if (cmd == "rm" || cmd == "rmdir" || cmd == "unlink") return {Tag::FsDelete};
  if (cmd == "cp" || cmd == "mv" || cmd == "mkdir" || cmd == "touch" || cmd == "ln")
    return {Tag::FsWrite};
  if (isInPlaceEditor(cmd, args)) return {Tag::FsWrite};
  if (cmd == "tee") return {Tag::FsWrite};
  if (cmd == "curl" || cmd == "wget" || cmd == "nc" || cmd == "ncat" ||
      cmd == "http" || cmd == "httpie")
    return {Tag::NetFetch};
  return {};
}

// Whether the segment has an unquoted redirect-write (> or >>).
bool hasRedirectWrite(std::string_view seg) {
  char quote = 0;
  for (std::size_t i = 0; i < seg.size(); ++i) {
    char c = seg[i];
    if (quote) {
      if (c == quote) quote = 0;
      continue;
    }
    if (c == '\'' || c == '"') {
      quote = c;
      continue;
    }
    if (c == '>') return true;
  }
  return false;
}

}  // namespace

std::vector<Tag> fromBash(std::string_view command) {
  std::vector<Tag> tags{Tag::Shell};
  auto add = [&](Tag t) {
    if (std::find(tags.begin(), tags.end(), t) == tags.end()) tags.push_back(t);
  };

  for (const auto& seg : splitSegments(command)) {
    if (hasRedirectWrite(seg)) add(Tag::FsWrite);
    auto toks = tokenize(seg);
    if (toks.empty()) continue;
    for (Tag t : classifySegment(toks)) add(t);
  }

  // Scan $() and ` inner commands recursively; union the tags.
  for (const auto& sub : commandSubs(command)) {
    if (sub.empty()) continue;
    for (Tag t : fromBash(sub)) add(t);
  }
  return tags;
}

}  // namespace core::cap
