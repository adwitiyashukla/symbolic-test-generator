#ifndef SYMX_EXEC_SEARCH_H
#define SYMX_EXEC_SEARCH_H

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "exec/state.h"
#include "util/support.h"

namespace symx {

enum class SearchStrategy { DepthFirst, BreadthFirst, Random, CoverageGuided };

bool parseSearchStrategy(const std::string &name, SearchStrategy &strategy);
const char *searchStrategyName(SearchStrategy strategy);

class Searcher {
 public:
  Searcher(SearchStrategy strategy, uint64_t seed);

  void add(std::unique_ptr<ExecutionState> state);
  ExecutionState *select();
  void remove(ExecutionState *state);
  size_t size() const { return states_.size(); }
  bool empty() const { return states_.empty(); }

 private:
  SearchStrategy strategy_;
  Rng rng_;
  std::vector<std::unique_ptr<ExecutionState>> states_;
};

}

#endif
