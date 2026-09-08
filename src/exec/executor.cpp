#include "exec/executor.h"

#include <algorithm>
#include <sstream>
#include <utility>

#include "util/support.h"

namespace symx {
namespace {

std::string locationKey(ErrorKind kind, const SourceLocation &location) {
  std::ostringstream os;
  os << static_cast<int>(kind) << ":" << location.line << ":" << location.column;
  return os.str();
}

uint64_t constantAddendSum(ExprRef address) {
  uint64_t total = 0;
  ExprRef current = address;
  while (current->kind == ExprKind::Add && isConstant(current->b)) {
    total += current->b->value;
    current = current->a;
  }
  if (isConstant(current)) {
    total += current->value;
  }
  return total;
}

}

const char *errorKindName(ErrorKind kind) {
  switch (kind) {
    case ErrorKind::Assertion:
      return "assertion failure";
    case ErrorKind::DivideByZero:
      return "division by zero";
    case ErrorKind::DivisionOverflow:
      return "signed division overflow";
    case ErrorKind::OutOfBounds:
      return "out of bounds access";
    case ErrorKind::NullPointer:
      return "null pointer dereference";
    case ErrorKind::ShiftAmount:
      return "shift amount out of range";
    case ErrorKind::Unsupported:
      return "unsupported operation";
  }
  return "error";
}

Executor::Executor(const IrModule &module, const ExecutorOptions &options, Solver &solver)
    : module_(module), options_(options), solver_(solver) {}

bool Executor::prepare(std::string &error) {
  auto found = module_.functionIndex.find(options_.entryFunction);
  if (found == module_.functionIndex.end()) {
    error = "entry function " + options_.entryFunction + " not found";
    return false;
  }
  entryIndex_ = found->second;
  const IrFunction &function = module_.functions[entryIndex_];
  uint64_t offset = 0;
  for (size_t i = 0; i < function.parameterTypes.size(); i++) {
    TypeRef type = function.parameterTypes[i];
    InputRegion region;
    region.name = function.parameterNames[i].empty() ? ("arg" + std::to_string(i))
                                                     : function.parameterNames[i];
    region.type = type;
    region.offset = offset;
    if (isPointerType(type)) {
      TypeRef element = type->element;
      uint64_t elementSize = typeSizeInBytes(element);
      if (elementSize == 0) {
        elementSize = 1;
      }
      region.isBuffer = true;
      region.elementCount = options_.bufferElements;
      region.size = elementSize * options_.bufferElements;
    } else if (isIntegerType(type)) {
      region.size = typeSizeInBytes(type);
    } else {
      error = "unsupported parameter type " + typeName(type) + " on " + options_.entryFunction;
      return false;
    }
    offset += region.size;
    layout_.push_back(region);
  }
  inputSize_ = offset;
  branchCoverage_.assign(static_cast<size_t>(module_.branchCount) * 2, 0);
  return true;
}

std::unique_ptr<ExecutionState> Executor::makeInitialState(const std::vector<uint8_t> *concrete) {
  std::unique_ptr<ExecutionState> state(new ExecutionState());
  state->id = nextStateId_++;
  for (const IrGlobal &global : module_.globals) {
    uint64_t objectId =
        state->memory.create(global.size == 0 ? 1 : global.size, global.name, false, false);
    state->memory.fillWithBytes(objectId, global.initialBytes);
  }
  const IrFunction &function = module_.functions[entryIndex_];
  StackFrame frame;
  frame.functionIndex = entryIndex_;
  frame.registers.assign(function.registerWidths.size(), nullptr);
  state->stack.push_back(frame);
  for (size_t i = 0; i < layout_.size(); i++) {
    const InputRegion &region = layout_[i];
    uint64_t objectId = state->memory.create(region.size, region.name, true, false);
    if (concrete != nullptr) {
      std::vector<uint8_t> slice;
      for (uint64_t k = 0; k < region.size; k++) {
        uint64_t index = region.offset + k;
        slice.push_back(index < concrete->size() ? (*concrete)[index] : 0);
      }
      state->memory.fillWithBytes(objectId, slice);
    } else {
      unsigned firstSymbol = 0;
      for (uint64_t k = 0; k < region.size; k++) {
        unsigned symbolId = addSymbolByte(region.name, static_cast<unsigned>(k));
        if (k == 0) {
          firstSymbol = symbolId;
        }
        if (i == 0 && k == 0) {
          symbolBase_ = symbolId;
        }
      }
      state->memory.fillWithSymbols(objectId, firstSymbol);
    }
    int parameterRegister = function.parameterRegisters[i];
    if (region.isBuffer) {
      state->stack.back().registers[parameterRegister] =
          bv::constant(64, addressOfObject(objectId));
    } else {
      unsigned width = typeBitWidth(region.type);
      state->stack.back().registers[parameterRegister] =
          state->memory.readConcrete(objectId, 0, width / 8);
    }
  }
  state->block = 0;
  state->instruction = 0;
  return state;
}

void Executor::markBranch(ExecutionState &state, int branchId, bool taken) {
  if (branchId < 0) {
    return;
  }
  size_t index = static_cast<size_t>(branchId) * 2 + (taken ? 1 : 0);
  if (index < branchCoverage_.size() && branchCoverage_[index] == 0) {
    branchCoverage_[index] = 1;
    state.newBranches++;
    state.stagnation = 0;
  }
}

void Executor::markLine(const Instruction &instruction) {
  if (instruction.location.line > 0) {
    coveredLines_.insert(instruction.location.line);
  }
}

uint64_t Executor::coveredBranchCount() const {
  uint64_t total = 0;
  for (uint8_t value : branchCoverage_) {
    total += value;
  }
  return total;
}

bool Executor::feasible(ExecutionState &state, ExprRef condition) {
  if (isTrue(condition)) {
    return true;
  }
  if (isFalse(condition)) {
    return false;
  }
  return solver_.check(state.constraints, condition) == QueryResult::Sat;
}

void Executor::addConstraint(ExecutionState &state, ExprRef condition) {
  if (isTrue(condition)) {
    return;
  }
  for (ExprRef existing : state.constraints) {
    if (existing == condition) {
      return;
    }
  }
  state.constraints.push_back(condition);
}

void Executor::recordTest(ExecutionState &state, bool isError, ErrorKind kind,
                          const std::string &message, const SourceLocation &location,
                          ExprRef errorCondition) {
  if (tests_.size() >= options_.maxTests) {
    return;
  }
  if (isError) {
    std::string key = locationKey(kind, location);
    if (reportedErrors_.count(key) != 0) {
      return;
    }
    reportedErrors_.insert(key);
  } else if (state.newBranches == 0) {
    return;
  }
  if (concreteMode_) {
    GeneratedTest concreteTest;
    concreteTest.input = concreteInput_;
    concreteTest.isError = isError;
    concreteTest.errorKind = kind;
    concreteTest.message = message;
    concreteTest.location = location;
    concreteTest.newBranches = state.newBranches;
    concreteTest.pathLength = state.steps;
    tests_.push_back(concreteTest);
    if (isError) {
      stats_.errors++;
    }
    return;
  }
  std::vector<uint8_t> assignment;
  QueryResult result = solver_.solveForModel(state.constraints, errorCondition, assignment);
  if (result != QueryResult::Sat) {
    return;
  }
  GeneratedTest test;
  test.input.assign(inputSize_, 0);
  for (uint64_t i = 0; i < inputSize_; i++) {
    uint64_t index = symbolBase_ + i;
    if (index < assignment.size()) {
      test.input[i] = assignment[index];
    }
  }
  test.isError = isError;
  test.errorKind = kind;
  test.message = message;
  test.location = location;
  test.newBranches = state.newBranches;
  test.pathLength = state.steps;
  tests_.push_back(test);
  if (isError) {
    stats_.errors++;
  }
}

void Executor::terminate(ExecutionState &state, TerminationKind kind) {
  state.termination = kind;
  if (kind == TerminationKind::Returned) {
    stats_.completedPaths++;
    recordTest(state, false, ErrorKind::Assertion, "path", SourceLocation(), bv::truth());
  } else if (kind == TerminationKind::Limited) {
    stats_.limitedPaths++;
  } else if (kind == TerminationKind::Infeasible) {
    stats_.infeasiblePaths++;
  }
}

bool Executor::resolveAddress(ExecutionState &state, ExprRef address, unsigned bytes,
                              const Instruction &instruction, uint64_t &objectId, ExprRef &offset) {
  uint64_t base = constantAddendSum(address);
  objectId = objectIdOfAddress(base);
  if (!isConstant(address) && !state.memory.valid(objectId)) {
    std::vector<uint8_t> assignment;
    if (solver_.solveForModel(state.constraints, bv::truth(), assignment) == QueryResult::Sat) {
      uint64_t concreteAddress = evaluateExpr(address, assignment);
      objectId = objectIdOfAddress(concreteAddress);
      ExprRef pinned =
          bv::eq(bv::lshr(address, bv::constant(64, kObjectShift)), bv::constant(64, objectId));
      if (feasible(state, pinned)) {
        addConstraint(state, pinned);
        stats_.pointerConcretizations++;
      }
    }
  }
  if (objectId == 0) {
    recordTest(state, true, ErrorKind::NullPointer, "null pointer dereference",
               instruction.location, bv::truth());
    terminate(state, TerminationKind::Error);
    return false;
  }
  if (!state.memory.valid(objectId)) {
    recordTest(state, true, ErrorKind::OutOfBounds, "pointer does not reference a live object",
               instruction.location, bv::truth());
    terminate(state, TerminationKind::Error);
    return false;
  }
  const MemoryObject &object = state.memory.object(objectId);
  ExprRef delta = bv::sub(address, bv::constant(64, addressOfObject(objectId)));
  offset = bv::extract(delta, 0, 32);
  if (object.size < bytes) {
    recordTest(state, true, ErrorKind::OutOfBounds, "access larger than the target object",
               instruction.location, bv::truth());
    terminate(state, TerminationKind::Error);
    return false;
  }
  ExprRef limit = bv::constant(64, object.size - bytes);
  ExprRef inBounds = bv::ule(delta, limit);
  if (isTrue(inBounds)) {
    return true;
  }
  ExprRef outOfBounds = bv::logicalNot(inBounds);
  if (feasible(state, outOfBounds)) {
    std::ostringstream os;
    os << "access " << bytes << " bytes outside " << object.name << " of size " << object.size;
    recordTest(state, true, ErrorKind::OutOfBounds, os.str(), instruction.location, outOfBounds);
    if (!feasible(state, inBounds)) {
      terminate(state, TerminationKind::Error);
      return false;
    }
  }
  addConstraint(state, inBounds);
  return true;
}

ExprRef Executor::applyBinary(const Instruction &instruction, ExprRef a, ExprRef b) {
  switch (instruction.binary) {
    case IrBinary::Add:
      return bv::add(a, b);
    case IrBinary::Sub:
      return bv::sub(a, b);
    case IrBinary::Mul:
      return bv::mul(a, b);
    case IrBinary::UDiv:
      return bv::udiv(a, b);
    case IrBinary::SDiv:
      return bv::sdiv(a, b);
    case IrBinary::URem:
      return bv::urem(a, b);
    case IrBinary::SRem:
      return bv::srem(a, b);
    case IrBinary::And:
      return bv::bitAnd(a, b);
    case IrBinary::Or:
      return bv::bitOr(a, b);
    case IrBinary::Xor:
      return bv::bitXor(a, b);
    case IrBinary::Shl:
      return bv::shl(a, b);
    case IrBinary::LShr:
      return bv::lshr(a, b);
    case IrBinary::AShr:
      return bv::ashr(a, b);
    case IrBinary::Equal:
      return bv::eq(a, b);
    case IrBinary::NotEqual:
      return bv::ne(a, b);
    case IrBinary::UnsignedLess:
      return bv::ult(a, b);
    case IrBinary::UnsignedLessEqual:
      return bv::ule(a, b);
    case IrBinary::SignedLess:
      return bv::slt(a, b);
    case IrBinary::SignedLessEqual:
      return bv::sle(a, b);
  }
  return bv::constant(instruction.width, 0);
}

void Executor::enterFunction(ExecutionState &state, int functionIndex,
                             const std::vector<ExprRef> &args, int returnRegister) {
  const IrFunction &callee = module_.functions[functionIndex];
  StackFrame frame;
  frame.functionIndex = functionIndex;
  frame.registers.assign(callee.registerWidths.size(), nullptr);
  frame.returnRegister = returnRegister;
  frame.returnBlock = state.block;
  frame.returnInstruction = state.instruction;
  for (size_t i = 0; i < args.size() && i < callee.parameterRegisters.size(); i++) {
    frame.registers[callee.parameterRegisters[i]] = args[i];
  }
  state.stack.push_back(frame);
  state.block = 0;
  state.instruction = 0;
}

void Executor::step(ExecutionState &state) {
  const IrFunction &function = module_.functions[state.stack.back().functionIndex];
  if (state.block < 0 || state.block >= static_cast<int>(function.blocks.size())) {
    terminate(state, TerminationKind::Returned);
    return;
  }
  const BasicBlock &block = function.blocks[state.block];
  if (state.instruction >= static_cast<int>(block.instructions.size())) {
    terminate(state, TerminationKind::Returned);
    return;
  }
  const Instruction &instruction = block.instructions[state.instruction];
  state.instruction++;
  state.steps++;
  state.stagnation++;
  stats_.instructions++;
  markLine(instruction);

  switch (instruction.opcode) {
    case Opcode::Constant:
      state.writeRegister(instruction.result,
                          bv::constant(instruction.width, instruction.immediate));
      return;
    case Opcode::Alloca: {
      uint64_t size = instruction.immediate == 0 ? 1 : instruction.immediate;
      uint64_t objectId = state.memory.create(size, "local", false, true);
      state.writeRegister(instruction.result, bv::constant(64, addressOfObject(objectId)));
      return;
    }
    case Opcode::GlobalAddress: {
      uint64_t objectId = instruction.immediate + 1;
      state.writeRegister(instruction.result, bv::constant(64, addressOfObject(objectId)));
      return;
    }
    case Opcode::Load: {
      ExprRef address = state.readRegister(instruction.a);
      unsigned bytes = instruction.width / 8;
      if (bytes == 0) {
        bytes = 1;
      }
      uint64_t objectId = 0;
      ExprRef offset = nullptr;
      if (!resolveAddress(state, address, bytes, instruction, objectId, offset)) {
        return;
      }
      ExprRef value;
      if (isConstant(offset)) {
        value = state.memory.readConcrete(objectId, offset->value, bytes);
      } else {
        const MemoryObject &object = state.memory.object(objectId);
        if (object.size > options_.symbolicOffsetLimit) {
          std::vector<uint8_t> assignment;
          if (solver_.solveForModel(state.constraints, bv::truth(), assignment) !=
              QueryResult::Sat) {
            terminate(state, TerminationKind::Infeasible);
            return;
          }
          uint64_t concreteOffset = evaluateExpr(offset, assignment);
          addConstraint(state, bv::eq(offset, bv::constant(32, concreteOffset)));
          value = state.memory.readConcrete(objectId, concreteOffset, bytes);
        } else {
          stats_.symbolicOffsetReads++;
          value = state.memory.readSymbolic(objectId, offset, bytes);
        }
      }
      if (value->width < instruction.width) {
        value = bv::zext(value, instruction.width);
      }
      state.writeRegister(instruction.result, value);
      return;
    }
    case Opcode::Store: {
      ExprRef address = state.readRegister(instruction.a);
      ExprRef value = state.readRegister(instruction.b);
      unsigned bytes = instruction.width / 8;
      if (bytes == 0) {
        bytes = 1;
        value = bv::zext(value, 8);
      }
      uint64_t objectId = 0;
      ExprRef offset = nullptr;
      if (!resolveAddress(state, address, bytes, instruction, objectId, offset)) {
        return;
      }
      if (isConstant(offset)) {
        state.memory.writeConcrete(objectId, offset->value, value, bytes);
      } else {
        const MemoryObject &object = state.memory.object(objectId);
        if (object.size > options_.symbolicOffsetLimit) {
          std::vector<uint8_t> assignment;
          if (solver_.solveForModel(state.constraints, bv::truth(), assignment) !=
              QueryResult::Sat) {
            terminate(state, TerminationKind::Infeasible);
            return;
          }
          uint64_t concreteOffset = evaluateExpr(offset, assignment);
          addConstraint(state, bv::eq(offset, bv::constant(32, concreteOffset)));
          state.memory.writeConcrete(objectId, concreteOffset, value, bytes);
        } else {
          state.memory.writeSymbolic(objectId, offset, value, bytes);
        }
      }
      return;
    }
    case Opcode::Binary: {
      ExprRef a = state.readRegister(instruction.a);
      ExprRef b = state.readRegister(instruction.b);
      if (isDivisionOp(instruction.binary)) {
        ExprRef zero = bv::constant(b->width, 0);
        ExprRef isZero = bv::eq(b, zero);
        if (feasible(state, isZero)) {
          recordTest(state, true, ErrorKind::DivideByZero, "divisor can be zero",
                     instruction.location, isZero);
          ExprRef nonZero = bv::logicalNot(isZero);
          if (!feasible(state, nonZero)) {
            terminate(state, TerminationKind::Error);
            return;
          }
          addConstraint(state, nonZero);
        }
        if (instruction.binary == IrBinary::SDiv || instruction.binary == IrBinary::SRem) {
          ExprRef lowest = bv::constant(a->width, 1ULL << (a->width - 1));
          ExprRef minusOne = bv::constant(b->width, maskFor(b->width));
          ExprRef overflow = bv::logicalAnd(bv::eq(a, lowest), bv::eq(b, minusOne));
          if (feasible(state, overflow)) {
            recordTest(state, true, ErrorKind::DivisionOverflow,
                       "the smallest value divided by minus one overflows", instruction.location,
                       overflow);
            ExprRef safe = bv::logicalNot(overflow);
            if (!feasible(state, safe)) {
              terminate(state, TerminationKind::Error);
              return;
            }
            addConstraint(state, safe);
          }
        }
      }
      if (options_.checkShiftAmount && isShiftOp(instruction.binary)) {
        ExprRef limit = bv::constant(b->width, instruction.width);
        ExprRef tooLarge = bv::uge(b, limit);
        if (feasible(state, tooLarge)) {
          recordTest(state, true, ErrorKind::ShiftAmount, "shift amount can exceed the width",
                     instruction.location, tooLarge);
          ExprRef valid = bv::logicalNot(tooLarge);
          if (!feasible(state, valid)) {
            terminate(state, TerminationKind::Error);
            return;
          }
          addConstraint(state, valid);
        }
      }
      state.writeRegister(instruction.result, applyBinary(instruction, a, b));
      return;
    }
    case Opcode::Cast: {
      ExprRef source = state.readRegister(instruction.a);
      state.writeRegister(instruction.result,
                          bv::resize(source, instruction.width, instruction.sourceSigned));
      return;
    }
    case Opcode::Call: {
      if (state.stack.size() >= options_.maxCallDepth) {
        terminate(state, TerminationKind::Limited);
        return;
      }
      std::vector<ExprRef> args;
      args.reserve(instruction.arguments.size());
      for (int argument : instruction.arguments) {
        args.push_back(state.readRegister(argument));
      }
      state.instruction--;
      enterFunction(state, static_cast<int>(instruction.immediate), args, instruction.result);
      return;
    }
    case Opcode::Return: {
      ExprRef value = instruction.a >= 0 ? state.readRegister(instruction.a) : nullptr;
      if (state.stack.size() == 1) {
        lastReturnDefined_ = value != nullptr && isConstant(value);
        if (lastReturnDefined_) {
          lastReturnValue_ = signExtendFrom(value->value, value->width);
        }
        terminate(state, TerminationKind::Returned);
        return;
      }
      StackFrame frame = state.stack.back();
      state.stack.pop_back();
      state.block = frame.returnBlock;
      state.instruction = frame.returnInstruction + 1;
      if (frame.returnRegister >= 0 && value != nullptr) {
        unsigned expected = module_.functions[state.stack.back().functionIndex]
                                .registerWidths[frame.returnRegister];
        state.writeRegister(frame.returnRegister, bv::resize(value, expected, false));
      } else if (frame.returnRegister >= 0) {
        unsigned expected = module_.functions[state.stack.back().functionIndex]
                                .registerWidths[frame.returnRegister];
        state.writeRegister(frame.returnRegister, bv::constant(expected, 0));
      }
      return;
    }
    case Opcode::Jump:
      state.block = instruction.targetTrue;
      state.instruction = 0;
      return;
    case Opcode::Branch: {
      ExprRef condition = state.readRegister(instruction.a);
      if (isConstant(condition)) {
        bool taken = condition->value != 0;
        markBranch(state, instruction.branchId, taken);
        state.block = taken ? instruction.targetTrue : instruction.targetFalse;
        state.instruction = 0;
        return;
      }
      ExprRef negated = bv::logicalNot(condition);
      bool canTake = feasible(state, condition);
      bool canSkip = feasible(state, negated);
      if (!canTake && !canSkip) {
        terminate(state, TerminationKind::Infeasible);
        return;
      }
      if (canTake && canSkip && state.forks < options_.maxForksPerPath && searcher_ != nullptr &&
          searcher_->size() < options_.maxStates) {
        std::unique_ptr<ExecutionState> forked(new ExecutionState(state));
        forked->id = nextStateId_++;
        forked->forks = state.forks + 1;
        stats_.forks++;
        stats_.statesCreated++;
        markBranch(*forked, instruction.branchId, false);
        addConstraint(*forked, negated);
        forked->block = instruction.targetFalse;
        forked->instruction = 0;
        searcher_->add(std::move(forked));

        state.forks++;
        markBranch(state, instruction.branchId, true);
        addConstraint(state, condition);
        state.block = instruction.targetTrue;
        state.instruction = 0;
        return;
      }
      bool taken = canTake;
      markBranch(state, instruction.branchId, taken);
      addConstraint(state, taken ? condition : negated);
      state.block = taken ? instruction.targetTrue : instruction.targetFalse;
      state.instruction = 0;
      return;
    }
    case Opcode::Assume: {
      ExprRef condition = state.readRegister(instruction.a);
      if (!feasible(state, condition)) {
        terminate(state, TerminationKind::Infeasible);
        return;
      }
      addConstraint(state, condition);
      return;
    }
    case Opcode::Assert: {
      ExprRef condition = state.readRegister(instruction.a);
      ExprRef violated = bv::logicalNot(condition);
      if (feasible(state, violated)) {
        recordTest(state, true, ErrorKind::Assertion, "assertion can fail", instruction.location,
                   violated);
        if (!feasible(state, condition)) {
          terminate(state, TerminationKind::Error);
          return;
        }
      }
      addConstraint(state, condition);
      return;
    }
    case Opcode::Unreachable:
      terminate(state, TerminationKind::Returned);
      return;
  }
}

void Executor::explore() {
  timer_.reset();
  searcher_.reset(new Searcher(options_.strategy, options_.seed));
  searcher_->add(makeInitialState(nullptr));
  stats_.statesCreated++;
  while (!searcher_->empty()) {
    if (stats_.instructions >= options_.maxSteps || timer_.seconds() >= options_.maxSeconds) {
      stats_.hitLimit = true;
      break;
    }
    ExecutionState *state = searcher_->select();
    if (state == nullptr) {
      break;
    }
    if (state->steps >= options_.maxSteps) {
      terminate(*state, TerminationKind::Limited);
    } else {
      step(*state);
    }
    if (state->termination != TerminationKind::Running) {
      searcher_->remove(state);
    }
  }
  stats_.seconds = timer_.seconds();
}

bool Executor::runConcrete(const std::vector<uint8_t> &input) {
  concreteMode_ = true;
  concreteInput_ = input;
  lastReturnDefined_ = false;
  lastReturnValue_ = 0;
  searcher_.reset();
  std::unique_ptr<ExecutionState> state = makeInitialState(&input);
  uint64_t budget = options_.maxSteps;
  while (state->termination == TerminationKind::Running && budget > 0) {
    step(*state);
    budget--;
  }
  concreteMode_ = false;
  return state->termination != TerminationKind::Error;
}

}
