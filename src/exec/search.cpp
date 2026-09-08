#include "exec/search.h"

#include <utility>

namespace symx {

bool parseSearchStrategy(const std::string &name, SearchStrategy &strategy) {
  if (name == "dfs") {
    strategy = SearchStrategy::DepthFirst;
    return true;
  }
  if (name == "bfs") {
    strategy = SearchStrategy::BreadthFirst;
    return true;
  }
  if (name == "random") {
    strategy = SearchStrategy::Random;
    return true;
  }
  if (name == "coverage") {
    strategy = SearchStrategy::CoverageGuided;
    return true;
  }
  return false;
}

const char *searchStrategyName(SearchStrategy strategy) {
  switch (strategy) {
    case SearchStrategy::DepthFirst:
      return "dfs";
    case SearchStrategy::BreadthFirst:
      return "bfs";
    case SearchStrategy::Random:
      return "random";
    case SearchStrategy::CoverageGuided:
      return "coverage";
  }
  return "?";
}

Searcher::Searcher(SearchStrategy strategy, uint64_t seed) : strategy_(strategy), rng_(seed) {}

void Searcher::add(std::unique_ptr<ExecutionState> state) { states_.push_back(std::move(state)); }

ExecutionState *Searcher::select() {
  if (states_.empty()) {
    return nullptr;
  }
  switch (strategy_) {
    case SearchStrategy::DepthFirst:
      return states_.back().get();
    case SearchStrategy::BreadthFirst:
      return states_.front().get();
    case SearchStrategy::Random:
      return states_[rng_.below(static_cast<uint32_t>(states_.size()))].get();
    case SearchStrategy::CoverageGuided: {
      if (rng_.below(100) < 25) {
        return states_[rng_.below(static_cast<uint32_t>(states_.size()))].get();
      }
      size_t best = 0;
      for (size_t i = 1; i < states_.size(); i++) {
        if (states_[i]->stagnation < states_[best]->stagnation) {
          best = i;
        }
      }
      return states_[best].get();
    }
  }
  return states_.back().get();
}

void Searcher::remove(ExecutionState *state) {
  for (size_t i = 0; i < states_.size(); i++) {
    if (states_[i].get() == state) {
      states_[i] = std::move(states_.back());
      states_.pop_back();
      return;
    }
  }
}

}
