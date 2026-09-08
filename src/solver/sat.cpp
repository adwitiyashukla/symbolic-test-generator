#include "solver/sat.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <sstream>
#include <unordered_map>

namespace symx {

SatSolver::SatSolver() {}

int SatSolver::newVar() {
  int var = static_cast<int>(assigns_.size());
  assigns_.push_back(kUndefined);
  savedPhase_.push_back(0);
  level_.push_back(0);
  reason_.push_back(kNoReason);
  activity_.push_back(0.0);
  seen_.push_back(0);
  heapPosition_.push_back(-1);
  watches_.push_back({});
  watches_.push_back({});
  heapInsert(var);
  return var;
}

uint8_t SatSolver::valueOfLit(int lit) const {
  uint8_t value = assigns_[litVar(lit)];
  if (value == kUndefined) {
    return kUndefined;
  }
  if (!litNegated(lit)) {
    return value;
  }
  return value == kTrue ? kFalse : kTrue;
}

void SatSolver::setClauseLbd(int ref, int lbd) {
  int flags = arena_[ref + 1] & (kDeletedFlag | kLearntFlag);
  arena_[ref + 1] = flags | (lbd & kLbdMask);
}

int SatSolver::allocateClause(const std::vector<int> &literals, bool learnt, int lbd) {
  int ref = static_cast<int>(arena_.size());
  arena_.push_back(static_cast<int>(literals.size()));
  arena_.push_back((learnt ? kLearntFlag : 0) | (lbd & kLbdMask));
  for (int lit : literals) {
    arena_.push_back(lit);
  }
  return ref;
}

void SatSolver::attachClause(int ref) {
  int first = clauseLit(ref, 0);
  int second = clauseLit(ref, 1);
  watches_[first].push_back({ref, second});
  watches_[second].push_back({ref, first});
}

void SatSolver::rebuildWatches() {
  for (auto &list : watches_) {
    list.clear();
  }
  for (int ref : clauses_) {
    attachClause(ref);
  }
  for (int ref : learnts_) {
    attachClause(ref);
  }
}

void SatSolver::compactArena() {
  std::vector<int> fresh;
  std::unordered_map<int, int> mapping;
  auto copyClause = [&](int ref) {
    int newRef = static_cast<int>(fresh.size());
    int size = clauseSize(ref);
    fresh.push_back(size);
    fresh.push_back(arena_[ref + 1]);
    for (int k = 0; k < size; k++) {
      fresh.push_back(clauseLit(ref, k));
    }
    mapping[ref] = newRef;
  };
  for (int ref : clauses_) {
    copyClause(ref);
  }
  for (int ref : learnts_) {
    copyClause(ref);
  }
  for (int &ref : clauses_) {
    ref = mapping[ref];
  }
  for (int &ref : learnts_) {
    ref = mapping[ref];
  }
  for (int &ref : reason_) {
    if (ref != kNoReason) {
      auto it = mapping.find(ref);
      ref = it == mapping.end() ? kNoReason : it->second;
    }
  }
  arena_.swap(fresh);
}

bool SatSolver::addUnit(int literal) { return addClause({literal}); }

bool SatSolver::addClause(std::vector<int> literals) {
  if (unsatisfiable_) {
    return false;
  }
  std::sort(literals.begin(), literals.end());
  std::vector<int> cleaned;
  int previous = -1;
  for (int lit : literals) {
    if (lit == previous) {
      continue;
    }
    if (previous != -1 && lit == litNegate(previous)) {
      return true;
    }
    uint8_t value = valueOfLit(lit);
    if (value == kTrue && level_[litVar(lit)] == 0) {
      return true;
    }
    if (value == kFalse && level_[litVar(lit)] == 0) {
      previous = lit;
      continue;
    }
    cleaned.push_back(lit);
    previous = lit;
  }
  if (cleaned.empty()) {
    unsatisfiable_ = true;
    return false;
  }
  if (cleaned.size() == 1) {
    assign(cleaned[0], kNoReason);
    if (propagate() != kNoReason) {
      unsatisfiable_ = true;
      return false;
    }
    return true;
  }
  int ref = allocateClause(cleaned, false, static_cast<int>(cleaned.size()));
  clauses_.push_back(ref);
  attachClause(ref);
  return true;
}

void SatSolver::assign(int lit, int reason) {
  int var = litVar(lit);
  assigns_[var] = litNegated(lit) ? kFalse : kTrue;
  level_[var] = decisionLevel();
  reason_[var] = reason;
  trail_.push_back(lit);
}

int SatSolver::propagate() {
  int conflict = kNoReason;
  while (propagateHead_ < trail_.size()) {
    int assigned = trail_[propagateHead_++];
    int falseLit = litNegate(assigned);
    std::vector<Watcher> &list = watches_[falseLit];
    size_t readIndex = 0;
    size_t writeIndex = 0;
    while (readIndex < list.size()) {
      Watcher watcher = list[readIndex++];
      if (valueOfLit(watcher.blocker) == kTrue) {
        list[writeIndex++] = watcher;
        continue;
      }
      int ref = watcher.clause;
      if (clauseLit(ref, 0) == falseLit) {
        setClauseLit(ref, 0, clauseLit(ref, 1));
        setClauseLit(ref, 1, falseLit);
      }
      int first = clauseLit(ref, 0);
      if (first != watcher.blocker && valueOfLit(first) == kTrue) {
        list[writeIndex++] = {ref, first};
        continue;
      }
      int size = clauseSize(ref);
      bool moved = false;
      for (int k = 2; k < size; k++) {
        int candidate = clauseLit(ref, k);
        if (valueOfLit(candidate) != kFalse) {
          setClauseLit(ref, 1, candidate);
          setClauseLit(ref, k, falseLit);
          watches_[candidate].push_back({ref, first});
          moved = true;
          break;
        }
      }
      if (moved) {
        continue;
      }
      list[writeIndex++] = {ref, first};
      if (valueOfLit(first) == kFalse) {
        conflict = ref;
        propagateHead_ = trail_.size();
        while (readIndex < list.size()) {
          list[writeIndex++] = list[readIndex++];
        }
      } else {
        stats_.propagations++;
        assign(first, ref);
      }
    }
    list.resize(writeIndex);
  }
  return conflict;
}

bool SatSolver::literalRedundant(int lit, uint32_t abstractLevels) {
  analyzeStack_.clear();
  analyzeStack_.push_back(lit);
  size_t top = analyzeToClear_.size();
  while (!analyzeStack_.empty()) {
    int ref = reason_[litVar(analyzeStack_.back())];
    analyzeStack_.pop_back();
    int size = clauseSize(ref);
    for (int k = 1; k < size; k++) {
      int other = clauseLit(ref, k);
      int var = litVar(other);
      if (seen_[var] || level_[var] == 0) {
        continue;
      }
      if (reason_[var] != kNoReason && (abstractLevel(var) & abstractLevels) != 0) {
        seen_[var] = 1;
        analyzeStack_.push_back(other);
        analyzeToClear_.push_back(other);
      } else {
        for (size_t i = top; i < analyzeToClear_.size(); i++) {
          seen_[litVar(analyzeToClear_[i])] = 0;
        }
        analyzeToClear_.resize(top);
        return false;
      }
    }
  }
  return true;
}

void SatSolver::analyze(int conflict, std::vector<int> &learnt, int &backtrackLevel) {
  learnt.clear();
  learnt.push_back(0);
  int pathCount = 0;
  int current = -1;
  int index = static_cast<int>(trail_.size()) - 1;
  int ref = conflict;
  do {
    int size = clauseSize(ref);
    for (int k = current == -1 ? 0 : 1; k < size; k++) {
      int other = clauseLit(ref, k);
      int var = litVar(other);
      if (!seen_[var] && level_[var] > 0) {
        bumpVarActivity(var);
        seen_[var] = 1;
        if (level_[var] >= decisionLevel()) {
          pathCount++;
        } else {
          learnt.push_back(other);
        }
      }
    }
    while (!seen_[litVar(trail_[index--])]) {
    }
    current = trail_[index + 1];
    ref = reason_[litVar(current)];
    seen_[litVar(current)] = 0;
    pathCount--;
  } while (pathCount > 0);
  learnt[0] = litNegate(current);

  analyzeToClear_ = learnt;
  uint32_t abstractLevels = 0;
  for (size_t i = 1; i < learnt.size(); i++) {
    abstractLevels |= abstractLevel(litVar(learnt[i]));
  }
  size_t writeIndex = 1;
  for (size_t i = 1; i < learnt.size(); i++) {
    int var = litVar(learnt[i]);
    if (reason_[var] == kNoReason || !literalRedundant(learnt[i], abstractLevels)) {
      learnt[writeIndex++] = learnt[i];
    }
  }
  stats_.learnedLiterals += writeIndex;
  learnt.resize(writeIndex);
  for (int lit : analyzeToClear_) {
    seen_[litVar(lit)] = 0;
  }

  if (learnt.size() == 1) {
    backtrackLevel = 0;
    return;
  }
  size_t best = 1;
  for (size_t i = 2; i < learnt.size(); i++) {
    if (level_[litVar(learnt[i])] > level_[litVar(learnt[best])]) {
      best = i;
    }
  }
  int swap = learnt[1];
  learnt[1] = learnt[best];
  learnt[best] = swap;
  backtrackLevel = level_[litVar(learnt[1])];
}

void SatSolver::cancelUntil(int level) {
  if (decisionLevel() <= level) {
    return;
  }
  for (int i = static_cast<int>(trail_.size()) - 1; i >= trailLimit_[level]; i--) {
    int var = litVar(trail_[i]);
    savedPhase_[var] = assigns_[var] == kTrue ? 1 : 0;
    assigns_[var] = kUndefined;
    reason_[var] = kNoReason;
    heapInsert(var);
  }
  trail_.resize(static_cast<size_t>(trailLimit_[level]));
  trailLimit_.resize(static_cast<size_t>(level));
  propagateHead_ = trail_.size();
}

void SatSolver::bumpVarActivity(int var) {
  activity_[var] += varIncrement_;
  if (activity_[var] > 1e100) {
    for (double &value : activity_) {
      value *= 1e-100;
    }
    varIncrement_ *= 1e-100;
  }
  int position = heapPosition_[var];
  if (position >= 0) {
    heapPercolateUp(position);
  }
}

void SatSolver::decayVarActivity() { varIncrement_ /= varDecay_; }

bool SatSolver::locked(int ref) const {
  int first = clauseLit(ref, 0);
  return valueOfLit(first) == kTrue && reason_[litVar(first)] == ref;
}

int SatSolver::computeLbd(const std::vector<int> &literals) {
  std::vector<int> levels;
  levels.reserve(literals.size());
  for (int lit : literals) {
    levels.push_back(level_[litVar(lit)]);
  }
  std::sort(levels.begin(), levels.end());
  levels.erase(std::unique(levels.begin(), levels.end()), levels.end());
  return static_cast<int>(levels.size());
}

void SatSolver::reduceDatabase() {
  std::vector<int> ordered = learnts_;
  std::sort(ordered.begin(), ordered.end(),
            [&](int a, int b) { return clauseLbd(a) > clauseLbd(b); });
  size_t target = ordered.size() / 2;
  size_t removed = 0;
  for (size_t i = 0; i < ordered.size() && removed < target; i++) {
    int ref = ordered[i];
    if (clauseLbd(ref) <= 3 || clauseSize(ref) <= 2 || locked(ref)) {
      continue;
    }
    arena_[ref + 1] |= kDeletedFlag;
    removed++;
  }
  stats_.removedClauses += removed;
  std::vector<int> kept;
  kept.reserve(learnts_.size());
  for (int ref : learnts_) {
    if (!clauseDeleted(ref)) {
      kept.push_back(ref);
    }
  }
  learnts_.swap(kept);
  compactArena();
  rebuildWatches();
  maxLearnts_ += 500;
}

uint64_t SatSolver::nextRandom() {
  seed_ ^= seed_ << 13;
  seed_ ^= seed_ >> 7;
  seed_ ^= seed_ << 17;
  return seed_;
}

double SatSolver::lubyValue(int index) const {
  int size = 1;
  int sequence = 0;
  while (size < index + 1) {
    sequence++;
    size = 2 * size + 1;
  }
  int position = index;
  while (size - 1 != position) {
    size = (size - 1) >> 1;
    sequence--;
    position = position % size;
  }
  return std::pow(2.0, sequence);
}

void SatSolver::heapInsert(int var) {
  if (heapPosition_[var] >= 0) {
    return;
  }
  heap_.push_back(var);
  heapPosition_[var] = static_cast<int>(heap_.size()) - 1;
  heapPercolateUp(static_cast<int>(heap_.size()) - 1);
}

void SatSolver::heapPercolateUp(int position) {
  int var = heap_[position];
  while (position > 0) {
    int parent = (position - 1) >> 1;
    if (activity_[heap_[parent]] >= activity_[var]) {
      break;
    }
    heap_[position] = heap_[parent];
    heapPosition_[heap_[position]] = position;
    position = parent;
  }
  heap_[position] = var;
  heapPosition_[var] = position;
}

void SatSolver::heapPercolateDown(int position) {
  int var = heap_[position];
  int size = static_cast<int>(heap_.size());
  for (;;) {
    int left = 2 * position + 1;
    if (left >= size) {
      break;
    }
    int child = left;
    int right = left + 1;
    if (right < size && activity_[heap_[right]] > activity_[heap_[left]]) {
      child = right;
    }
    if (activity_[heap_[child]] <= activity_[var]) {
      break;
    }
    heap_[position] = heap_[child];
    heapPosition_[heap_[position]] = position;
    position = child;
  }
  heap_[position] = var;
  heapPosition_[var] = position;
}

int SatSolver::heapPop() {
  int top = heap_[0];
  heap_[0] = heap_.back();
  heapPosition_[heap_[0]] = 0;
  heap_.pop_back();
  heapPosition_[top] = -1;
  if (!heap_.empty()) {
    heapPercolateDown(0);
  }
  return top;
}

int SatSolver::pickBranchVar() {
  while (!heapEmpty()) {
    int var = heapPop();
    if (assigns_[var] == kUndefined) {
      return var;
    }
  }
  for (int var = 0; var < varCount(); var++) {
    if (assigns_[var] == kUndefined) {
      return var;
    }
  }
  return -1;
}

SatResult SatSolver::solve() { return solve({}); }

SatResult SatSolver::solve(const std::vector<int> &assumptions) {
  if (unsatisfiable_) {
    return SatResult::Unsat;
  }
  model_.clear();
  cancelUntil(0);
  if (maxLearnts_ == 0) {
    maxLearnts_ = std::max<size_t>(clauses_.size() / 3, 2000);
  }
  int64_t conflictsLeft = conflictBudget_;
  int restartIndex = 0;
  int64_t untilRestart = static_cast<int64_t>(lubyValue(restartIndex) * 100.0);
  std::vector<int> learnt;
  for (;;) {
    int conflict = propagate();
    if (conflict != kNoReason) {
      stats_.conflicts++;
      if (decisionLevel() == 0) {
        unsatisfiable_ = true;
        return SatResult::Unsat;
      }
      if (decisionLevel() <= static_cast<int>(assumptions.size())) {
        cancelUntil(0);
        return SatResult::Unsat;
      }
      int backtrackLevel = 0;
      analyze(conflict, learnt, backtrackLevel);
      cancelUntil(std::max(backtrackLevel, static_cast<int>(assumptions.size())));
      if (learnt.size() == 1 && decisionLevel() == 0) {
        assign(learnt[0], kNoReason);
      } else {
        int lbd = computeLbd(learnt);
        int ref = allocateClause(learnt, true, lbd);
        learnts_.push_back(ref);
        attachClause(ref);
        assign(learnt[0], ref);
      }
      decayVarActivity();
      untilRestart--;
      if (conflictsLeft > 0) {
        conflictsLeft--;
        if (conflictsLeft == 0) {
          cancelUntil(0);
          return SatResult::Unknown;
        }
      }
      continue;
    }
    if (untilRestart <= 0 && decisionLevel() > static_cast<int>(assumptions.size())) {
      restartIndex++;
      untilRestart = static_cast<int64_t>(lubyValue(restartIndex) * 100.0);
      stats_.restarts++;
      cancelUntil(static_cast<int>(assumptions.size()));
      continue;
    }
    if (learnts_.size() > maxLearnts_) {
      cancelUntil(static_cast<int>(assumptions.size()));
      reduceDatabase();
      continue;
    }
    int next = -1;
    while (decisionLevel() < static_cast<int>(assumptions.size())) {
      int assumption = assumptions[decisionLevel()];
      uint8_t value = valueOfLit(assumption);
      if (value == kTrue) {
        trailLimit_.push_back(static_cast<int>(trail_.size()));
      } else if (value == kFalse) {
        cancelUntil(0);
        return SatResult::Unsat;
      } else {
        next = assumption;
        break;
      }
    }
    if (next == -1) {
      int var = pickBranchVar();
      if (var == -1) {
        model_.assign(assigns_.begin(), assigns_.end());
        for (uint8_t &value : model_) {
          if (value == kUndefined) {
            value = kFalse;
          }
        }
        cancelUntil(0);
        return SatResult::Sat;
      }
      if ((nextRandom() % 100) < 2) {
        next = litFromVar(var, (nextRandom() & 1) != 0);
      } else {
        next = litFromVar(var, savedPhase_[var] == 0);
      }
    }
    stats_.decisions++;
    trailLimit_.push_back(static_cast<int>(trail_.size()));
    assign(next, kNoReason);
  }
}

bool SatSolver::modelValue(int var) const {
  if (var >= static_cast<int>(model_.size())) {
    return false;
  }
  return model_[var] == kTrue;
}

void SatSolver::detachAll() {
  for (auto &list : watches_) {
    list.clear();
  }
}

bool parseDimacs(const std::string &text, SatSolver &solver, int &varCount, std::string &error) {
  std::istringstream input(text);
  std::string line;
  varCount = 0;
  std::vector<int> clause;
  bool sawHeader = false;
  while (std::getline(input, line)) {
    if (line.empty() || line[0] == 'c' || line[0] == '%') {
      continue;
    }
    if (line[0] == 'p') {
      std::istringstream header(line);
      std::string p;
      std::string format;
      int variables = 0;
      int clauses = 0;
      header >> p >> format >> variables >> clauses;
      if (format != "cnf") {
        error = "unsupported dimacs format";
        return false;
      }
      for (int i = 0; i < variables; i++) {
        solver.newVar();
      }
      varCount = variables;
      sawHeader = true;
      continue;
    }
    std::istringstream values(line);
    int value = 0;
    while (values >> value) {
      if (value == 0) {
        if (!solver.addClause(clause)) {
          clause.clear();
          continue;
        }
        clause.clear();
        continue;
      }
      int variable = std::abs(value) - 1;
      while (variable >= solver.varCount()) {
        solver.newVar();
      }
      if (variable + 1 > varCount) {
        varCount = variable + 1;
      }
      clause.push_back(litFromVar(variable, value < 0));
    }
  }
  if (!clause.empty()) {
    solver.addClause(clause);
  }
  if (!sawHeader && varCount == 0) {
    error = "no clauses found";
    return false;
  }
  return true;
}

}
