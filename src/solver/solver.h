#ifndef SYMX_SOLVER_SOLVER_H
#define SYMX_SOLVER_SOLVER_H

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "solver/expr.h"

namespace symx {

enum class QueryResult { Sat, Unsat, Unknown };

struct SolverStats {
  uint64_t queries = 0;
  uint64_t cacheHits = 0;
  uint64_t satisfiable = 0;
  uint64_t unsatisfiable = 0;
  uint64_t unknown = 0;
  uint64_t constraintsDropped = 0;
  uint64_t satConflicts = 0;
  uint64_t satDecisions = 0;
  uint64_t gates = 0;
  double seconds = 0.0;
};

struct SolverOptions {
  bool useIndependence = true;
  bool useCache = true;
  int64_t conflictBudget = -1;
};

class ConstraintSet {
 public:
  void add(ExprRef condition);
  bool hasContradiction() const { return contradiction_; }
  const std::vector<ExprRef> &items() const { return items_; }
  size_t size() const { return items_.size(); }

 private:
  std::vector<ExprRef> items_;
  bool contradiction_ = false;
};

class Solver {
 public:
  explicit Solver(const SolverOptions &options = SolverOptions());

  QueryResult check(const std::vector<ExprRef> &constraints, ExprRef condition);
  QueryResult solveForModel(const std::vector<ExprRef> &constraints, ExprRef condition,
                            std::vector<uint8_t> &assignment);

  const SolverStats &stats() const { return stats_; }
  const SolverOptions &options() const { return options_; }

 private:
  std::vector<ExprRef> relevantConstraints(const std::vector<ExprRef> &constraints,
                                           ExprRef condition);
  QueryResult run(const std::vector<ExprRef> &constraints, ExprRef condition,
                  std::vector<uint8_t> *assignment);

  SolverOptions options_;
  SolverStats stats_;
  std::unordered_map<uint64_t, QueryResult> cache_;
};

}

#endif
