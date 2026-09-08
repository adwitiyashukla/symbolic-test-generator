#ifndef SYMX_SOLVER_SAT_H
#define SYMX_SOLVER_SAT_H

#include <cstdint>
#include <string>
#include <vector>

namespace symx {

inline int litFromVar(int var, bool negated) { return (var << 1) | (negated ? 1 : 0); }
inline int litVar(int lit) { return lit >> 1; }
inline bool litNegated(int lit) { return (lit & 1) != 0; }
inline int litNegate(int lit) { return lit ^ 1; }

enum class SatResult { Sat, Unsat, Unknown };

struct SatStats {
  uint64_t decisions = 0;
  uint64_t propagations = 0;
  uint64_t conflicts = 0;
  uint64_t restarts = 0;
  uint64_t learnedLiterals = 0;
  uint64_t removedClauses = 0;
};

class SatSolver {
 public:
  SatSolver();

  int newVar();
  int varCount() const { return static_cast<int>(assigns_.size()); }

  bool addClause(std::vector<int> literals);
  bool addUnit(int literal);

  SatResult solve();
  SatResult solve(const std::vector<int> &assumptions);

  bool modelValue(int var) const;
  const std::vector<uint8_t> &model() const { return model_; }

  void setConflictBudget(int64_t budget) { conflictBudget_ = budget; }
  void setSeed(uint64_t seed) { seed_ = seed ? seed : 1; }

  const SatStats &stats() const { return stats_; }

 private:
  static constexpr int kUndefined = 0;
  static constexpr int kTrue = 1;
  static constexpr int kFalse = 2;
  static constexpr int kNoReason = -1;
  static constexpr int kHeaderSize = 2;
  static constexpr int kDeletedFlag = 1 << 30;
  static constexpr int kLearntFlag = 1 << 29;
  static constexpr int kLbdMask = (1 << 29) - 1;

  struct Watcher {
    int clause;
    int blocker;
  };

  int clauseSize(int ref) const { return arena_[ref]; }
  int clauseLit(int ref, int index) const { return arena_[ref + kHeaderSize + index]; }
  void setClauseLit(int ref, int index, int lit) { arena_[ref + kHeaderSize + index] = lit; }
  bool clauseLearnt(int ref) const { return (arena_[ref + 1] & kLearntFlag) != 0; }
  bool clauseDeleted(int ref) const { return (arena_[ref + 1] & kDeletedFlag) != 0; }
  int clauseLbd(int ref) const { return arena_[ref + 1] & kLbdMask; }
  void setClauseLbd(int ref, int lbd);

  int allocateClause(const std::vector<int> &literals, bool learnt, int lbd);
  void attachClause(int ref);
  void detachAll();
  void rebuildWatches();
  void compactArena();

  uint8_t valueOfVar(int var) const { return assigns_[var]; }
  uint8_t valueOfLit(int lit) const;

  void assign(int lit, int reason);
  int propagate();
  void analyze(int conflict, std::vector<int> &learnt, int &backtrackLevel);
  bool literalRedundant(int lit, uint32_t abstractLevels);
  void cancelUntil(int level);
  int pickBranchVar();
  void bumpVarActivity(int var);
  void decayVarActivity();
  void reduceDatabase();
  bool locked(int ref) const;
  int decisionLevel() const { return static_cast<int>(trailLimit_.size()); }
  uint32_t abstractLevel(int var) const { return 1u << (static_cast<uint32_t>(level_[var]) & 31u); }
  int computeLbd(const std::vector<int> &literals);
  uint64_t nextRandom();
  double lubyValue(int index) const;

  void heapInsert(int var);
  void heapPercolateUp(int position);
  void heapPercolateDown(int position);
  int heapPop();
  bool heapEmpty() const { return heap_.empty(); }

  std::vector<int> arena_;
  std::vector<int> clauses_;
  std::vector<int> learnts_;
  std::vector<std::vector<Watcher>> watches_;
  std::vector<uint8_t> assigns_;
  std::vector<uint8_t> savedPhase_;
  std::vector<int> level_;
  std::vector<int> reason_;
  std::vector<double> activity_;
  std::vector<int> trail_;
  std::vector<int> trailLimit_;
  std::vector<uint8_t> seen_;
  std::vector<int> heap_;
  std::vector<int> heapPosition_;
  std::vector<uint8_t> model_;
  std::vector<int> analyzeStack_;
  std::vector<int> analyzeToClear_;

  size_t propagateHead_ = 0;
  double varIncrement_ = 1.0;
  double varDecay_ = 0.95;
  bool unsatisfiable_ = false;
  int64_t conflictBudget_ = -1;
  uint64_t seed_ = 0x2545f4914f6cdd1dULL;
  size_t maxLearnts_ = 0;
  SatStats stats_;
};

bool parseDimacs(const std::string &text, SatSolver &solver, int &varCount, std::string &error);

}

#endif
