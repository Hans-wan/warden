// Three-layer trajectory diff: capability set (deterministic), tool sequence
// (edit distance), products (hashes).
// The default CI assertion = no new capabilities.
#pragma once
#include <string>
#include <vector>

#include "core/journal/journal.h"

namespace core::replay {

struct Report {
  std::vector<std::string> addedCaps;
  std::vector<std::string> removedCaps;
  std::vector<std::string> productChanges;
  int toolSeqDist = 0;
  std::string verdict;  // clean / caps-changed / drift
};

std::vector<std::string> capSet(const std::vector<journal::Entry>& entries);
Report diff(const std::vector<journal::Entry>& base,
            const std::vector<journal::Entry>& target);

}  // namespace core::replay
