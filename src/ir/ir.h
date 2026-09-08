#ifndef SYMX_IR_IR_H
#define SYMX_IR_IR_H

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "ir/ast.h"
#include "ir/lexer.h"

namespace symx {

enum class Opcode {
  Constant,
  Alloca,
  GlobalAddress,
  Load,
  Store,
  Binary,
  Cast,
  Call,
  Return,
  Branch,
  Jump,
  Assume,
  Assert,
  Unreachable
};

enum class IrBinary {
  Add,
  Sub,
  Mul,
  UDiv,
  SDiv,
  URem,
  SRem,
  And,
  Or,
  Xor,
  Shl,
  LShr,
  AShr,
  Equal,
  NotEqual,
  UnsignedLess,
  UnsignedLessEqual,
  SignedLess,
  SignedLessEqual
};

const char *irBinaryName(IrBinary op);
bool isDivisionOp(IrBinary op);
bool isShiftOp(IrBinary op);

struct Instruction {
  Opcode opcode = Opcode::Unreachable;
  IrBinary binary = IrBinary::Add;
  int result = -1;
  int a = -1;
  int b = -1;
  unsigned width = 0;
  bool sourceSigned = false;
  uint64_t immediate = 0;
  std::vector<int> arguments;
  int targetTrue = -1;
  int targetFalse = -1;
  int branchId = -1;
  SourceLocation location;
};

struct BasicBlock {
  std::vector<Instruction> instructions;
};

struct IrFunction {
  std::string name;
  TypeRef returnType = nullptr;
  std::vector<TypeRef> parameterTypes;
  std::vector<std::string> parameterNames;
  std::vector<int> parameterRegisters;
  std::vector<unsigned> registerWidths;
  std::vector<BasicBlock> blocks;
  SourceLocation location;
};

struct IrGlobal {
  std::string name;
  uint64_t size = 0;
  std::vector<uint8_t> initialBytes;
};

struct IrModule {
  std::string fileName;
  std::vector<IrFunction> functions;
  std::vector<IrGlobal> globals;
  std::unordered_map<std::string, int> functionIndex;
  std::unordered_map<std::string, int> globalIndex;
  int branchCount = 0;
  int instructionCount = 0;
};

std::string describeInstruction(const IrModule &module, const Instruction &instruction);
std::string dumpFunction(const IrModule &module, const IrFunction &function);

}

#endif
