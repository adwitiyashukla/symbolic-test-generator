#include "solver/solver.h"

#include <algorithm>
#include <iterator>

#include "solver/bitblast.h"
#include "solver/sat.h"
#include "util/support.h"

namespace symx {
namespace {

uint64_t mixKey(uint64_t seed, uint64_t value) {
  seed ^= value + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2);
  return seed;
}

}

void ConstraintSet::add(ExprRef condition) {
  if (isTrue(condition)) {
    return;
  }
  if (symx::isFalse(condition)) {
    contradiction_ = true;
    items_.push_back(condition);
    return;
  }
  if (condition->kind == ExprKind::And && condition->width == 1) {
    add(condition->a);
    add(condition->b);
    return;
  }
  for (ExprRef existing : items_) {
    if (existing == condition) {
      return;
    }
  }
  items_.push_back(condition);
}

Solver::Solver(const SolverOptions &options) : options_(options) {}

std::vector<ExprRef> Solver::relevantConstraints(const std::vector<ExprRef> &constraints,
                                                 ExprRef condition) {
  if (!options_.useIndependence) {
    return constraints;
  }
  std::vector<std::vector<unsigned>> symbolsPer(constraints.size());
  for (size_t i = 0; i < constraints.size(); i++) {
    collectSymbols(constraints[i], symbolsPer[i]);
    std::sort(symbolsPer[i].begin(), symbolsPer[i].end());
    symbolsPer[i].erase(std::unique(symbolsPer[i].begin(), symbolsPer[i].end()),
                        symbolsPer[i].end());
  }
  std::vector<unsigned> frontier;
  collectSymbols(condition, frontier);
  std::sort(frontier.begin(), frontier.end());
  frontier.erase(std::unique(frontier.begin(), frontier.end()), frontier.end());

  std::vector<bool> taken(constraints.size(), false);
  bool changed = true;
  while (changed) {
    changed = false;
    for (size_t i = 0; i < constraints.size(); i++) {
      if (taken[i]) {
        continue;
      }
      bool overlaps = false;
      for (unsigned symbol : symbolsPer[i]) {
        if (std::binary_search(frontier.begin(), frontier.end(), symbol)) {
          overlaps = true;
          break;
        }
      }
      if (!overlaps) {
        continue;
      }
      taken[i] = true;
      changed = true;
      std::vector<unsigned> merged;
      std::set_union(frontier.begin(), frontier.end(), symbolsPer[i].begin(), symbolsPer[i].end(),
                     std::back_inserter(merged));
      frontier.swap(merged);
    }
  }
  std::vector<ExprRef> result;
  for (size_t i = 0; i < constraints.size(); i++) {
    if (taken[i]) {
      result.push_back(constraints[i]);
    } else {
      stats_.constraintsDropped++;
    }
  }
  return result;
}

QueryResult Solver::run(const std::vector<ExprRef> &constraints, ExprRef condition,
                        std::vector<uint8_t> *assignment) {
  Timer timer;
  SatSolver sat;
  sat.setConflictBudget(options_.conflictBudget);
  BitBlaster blaster(sat);
  for (ExprRef constraint : constraints) {
    blaster.assertTrue(constraint);
  }
  if (!isTrue(condition)) {
    blaster.assertTrue(condition);
  }
  SatResult result = sat.solve();
  stats_.seconds += timer.seconds();
  stats_.satConflicts += sat.stats().conflicts;
  stats_.satDecisions += sat.stats().decisions;
  stats_.gates += blaster.gateCount();
  if (result == SatResult::Unknown) {
    stats_.unknown++;
    return QueryResult::Unknown;
  }
  if (result == SatResult::Unsat) {
    stats_.unsatisfiable++;
    return QueryResult::Unsat;
  }
  stats_.satisfiable++;
  if (assignment != nullptr) {
    assignment->assign(symbolCount(), 0);
    for (unsigned symbolId : blaster.usedSymbols()) {
      (*assignment)[symbolId] = blaster.symbolValue(symbolId);
    }
  }
  return QueryResult::Sat;
}

QueryResult Solver::check(const std::vector<ExprRef> &constraints, ExprRef condition) {
  stats_.queries++;
  if (isFalse(condition)) {
    stats_.unsatisfiable++;
    return QueryResult::Unsat;
  }
  for (ExprRef constraint : constraints) {
    if (isFalse(constraint)) {
      stats_.unsatisfiable++;
      return QueryResult::Unsat;
    }
  }
  std::vector<ExprRef> relevant = relevantConstraints(constraints, condition);
  uint64_t key = 0;
  if (options_.useCache) {
    std::vector<uint64_t> parts;
    parts.reserve(relevant.size() + 1);
    for (ExprRef constraint : relevant) {
      parts.push_back(constraint->hash);
    }
    parts.push_back(condition->hash);
    std::sort(parts.begin(), parts.end() - 1);
    key = 1469598103934665603ULL;
    for (uint64_t part : parts) {
      key = mixKey(key, part);
    }
    auto found = cache_.find(key);
    if (found != cache_.end()) {
      stats_.cacheHits++;
      return found->second;
    }
  }
  QueryResult result = run(relevant, condition, nullptr);
  if (options_.useCache) {
    cache_.emplace(key, result);
  }
  return result;
}

QueryResult Solver::solveForModel(const std::vector<ExprRef> &constraints, ExprRef condition,
                                  std::vector<uint8_t> &assignment) {
  stats_.queries++;
  if (isFalse(condition)) {
    stats_.unsatisfiable++;
    return QueryResult::Unsat;
  }
  return run(constraints, condition, &assignment);
}

}
