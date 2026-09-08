#ifndef SYMX_EXEC_STATE_H
#define SYMX_EXEC_STATE_H

#include <cstdint>
#include <vector>

#include "exec/memory.h"
#include "solver/expr.h"

namespace symx {

struct StackFrame {
  int functionIndex = 0;
  std::vector<ExprRef> registers;
  int returnRegister = -1;
  int returnBlock = -1;
  int returnInstruction = -1;
};

enum class TerminationKind { Running, Returned, Error, Limited, Infeasible };

struct ExecutionState {
  Memory memory;
  std::vector<StackFrame> stack;
  std::vector<ExprRef> constraints;
  int block = 0;
  int instruction = 0;
  uint64_t id = 0;
  uint64_t steps = 0;
  uint64_t forks = 0;
  uint64_t stagnation = 0;
  uint64_t newBranches = 0;
  TerminationKind termination = TerminationKind::Running;

  ExprRef readRegister(int index) const { return stack.back().registers[index]; }
  void writeRegister(int index, ExprRef value) { stack.back().registers[index] = value; }
};

}

#endif
