#ifndef SYMX_EXEC_EXECUTOR_H
#define SYMX_EXEC_EXECUTOR_H

#include <cstdint>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "exec/search.h"
#include "exec/state.h"
#include "ir/ir.h"
#include "solver/solver.h"

namespace symx {

enum class ErrorKind {
  Assertion,
  DivideByZero,
  DivisionOverflow,
  OutOfBounds,
  NullPointer,
  ShiftAmount,
  Unsupported
};

const char *errorKindName(ErrorKind kind);

struct ExecutorOptions {
  std::string entryFunction = "check";
  SearchStrategy strategy = SearchStrategy::CoverageGuided;
  uint64_t bufferElements = 8;
  uint64_t maxSteps = 2000000;
  uint64_t maxStates = 4000;
  uint64_t maxForksPerPath = 2000;
  uint64_t maxCallDepth = 64;
  uint64_t maxTests = 512;
  uint64_t symbolicOffsetLimit = 512;
  double maxSeconds = 60.0;
  uint64_t seed = 20260908;
  bool checkShiftAmount = false;
};

struct InputRegion {
  std::string name;
  uint64_t offset = 0;
  uint64_t size = 0;
  TypeRef type = nullptr;
  bool isBuffer = false;
  uint64_t elementCount = 1;
};

struct GeneratedTest {
  std::vector<uint8_t> input;
  bool isError = false;
  ErrorKind errorKind = ErrorKind::Assertion;
  std::string message;
  SourceLocation location;
  uint64_t newBranches = 0;
  uint64_t pathLength = 0;
};

struct ExplorationStats {
  uint64_t statesCreated = 0;
  uint64_t forks = 0;
  uint64_t instructions = 0;
  uint64_t completedPaths = 0;
  uint64_t limitedPaths = 0;
  uint64_t infeasiblePaths = 0;
  uint64_t errors = 0;
  uint64_t pointerConcretizations = 0;
  uint64_t symbolicOffsetReads = 0;
  double seconds = 0.0;
  bool hitLimit = false;
};

class Executor {
 public:
  Executor(const IrModule &module, const ExecutorOptions &options, Solver &solver);

  bool prepare(std::string &error);
  void explore();
  bool runConcrete(const std::vector<uint8_t> &input);
  bool lastReturnDefined() const { return lastReturnDefined_; }
  uint64_t lastReturnValue() const { return lastReturnValue_; }

  const std::vector<GeneratedTest> &tests() const { return tests_; }
  const std::vector<InputRegion> &inputLayout() const { return layout_; }
  uint64_t inputSize() const { return inputSize_; }
  const std::vector<uint8_t> &branchCoverage() const { return branchCoverage_; }
  uint64_t coveredBranchCount() const;
  uint64_t totalBranchSlots() const { return branchCoverage_.size(); }
  const std::set<int> &coveredLines() const { return coveredLines_; }
  const ExplorationStats &stats() const { return stats_; }
  const IrFunction &entry() const { return module_.functions[entryIndex_]; }

 private:
  std::unique_ptr<ExecutionState> makeInitialState(const std::vector<uint8_t> *concrete);
  void step(ExecutionState &state);
  void terminate(ExecutionState &state, TerminationKind kind);
  void recordTest(ExecutionState &state, bool isError, ErrorKind kind, const std::string &message,
                  const SourceLocation &location, ExprRef errorCondition);
  bool feasible(ExecutionState &state, ExprRef condition);
  void addConstraint(ExecutionState &state, ExprRef condition);
  bool resolveAddress(ExecutionState &state, ExprRef address, unsigned bytes,
                      const Instruction &instruction, uint64_t &objectId, ExprRef &offset);
  void markBranch(ExecutionState &state, int branchId, bool taken);
  void markLine(const Instruction &instruction);
  ExprRef applyBinary(const Instruction &instruction, ExprRef a, ExprRef b);
  void enterFunction(ExecutionState &state, int functionIndex, const std::vector<ExprRef> &args,
                     int returnRegister);

  const IrModule &module_;
  ExecutorOptions options_;
  Solver &solver_;
  int entryIndex_ = -1;
  uint64_t inputSize_ = 0;
  unsigned symbolBase_ = 0;
  std::vector<InputRegion> layout_;
  std::vector<uint8_t> branchCoverage_;
  std::set<int> coveredLines_;
  std::set<std::string> reportedErrors_;
  std::vector<GeneratedTest> tests_;
  std::unique_ptr<Searcher> searcher_;
  ExplorationStats stats_;
  Timer timer_;
  uint64_t nextStateId_ = 1;
  bool concreteMode_ = false;
  bool lastReturnDefined_ = false;
  uint64_t lastReturnValue_ = 0;
  std::vector<uint8_t> concreteInput_;
};

}

#endif
