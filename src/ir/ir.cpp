#include "ir/ir.h"

#include <sstream>

namespace symx {

const char *irBinaryName(IrBinary op) {
  switch (op) {
    case IrBinary::Add:
      return "add";
    case IrBinary::Sub:
      return "sub";
    case IrBinary::Mul:
      return "mul";
    case IrBinary::UDiv:
      return "udiv";
    case IrBinary::SDiv:
      return "sdiv";
    case IrBinary::URem:
      return "urem";
    case IrBinary::SRem:
      return "srem";
    case IrBinary::And:
      return "and";
    case IrBinary::Or:
      return "or";
    case IrBinary::Xor:
      return "xor";
    case IrBinary::Shl:
      return "shl";
    case IrBinary::LShr:
      return "lshr";
    case IrBinary::AShr:
      return "ashr";
    case IrBinary::Equal:
      return "eq";
    case IrBinary::NotEqual:
      return "ne";
    case IrBinary::UnsignedLess:
      return "ult";
    case IrBinary::UnsignedLessEqual:
      return "ule";
    case IrBinary::SignedLess:
      return "slt";
    case IrBinary::SignedLessEqual:
      return "sle";
  }
  return "?";
}

bool isDivisionOp(IrBinary op) {
  return op == IrBinary::UDiv || op == IrBinary::SDiv || op == IrBinary::URem ||
         op == IrBinary::SRem;
}

bool isShiftOp(IrBinary op) {
  return op == IrBinary::Shl || op == IrBinary::LShr || op == IrBinary::AShr;
}

std::string describeInstruction(const IrModule &module, const Instruction &instruction) {
  std::ostringstream os;
  if (instruction.result >= 0) {
    os << "r" << instruction.result << " = ";
  }
  switch (instruction.opcode) {
    case Opcode::Constant:
      os << "const " << instruction.immediate << " : i" << instruction.width;
      break;
    case Opcode::Alloca:
      os << "alloca " << instruction.immediate;
      break;
    case Opcode::GlobalAddress:
      os << "global " << module.globals[instruction.immediate].name;
      break;
    case Opcode::Load:
      os << "load i" << instruction.width << " r" << instruction.a;
      break;
    case Opcode::Store:
      os << "store i" << instruction.width << " r" << instruction.b << " to r" << instruction.a;
      break;
    case Opcode::Binary:
      os << irBinaryName(instruction.binary) << " r" << instruction.a << " r" << instruction.b;
      break;
    case Opcode::Cast:
      os << "cast to i" << instruction.width << " r" << instruction.a
         << (instruction.sourceSigned ? " signed" : " unsigned");
      break;
    case Opcode::Call:
      os << "call " << module.functions[instruction.immediate].name;
      for (int argument : instruction.arguments) {
        os << " r" << argument;
      }
      break;
    case Opcode::Return:
      os << "return";
      if (instruction.a >= 0) {
        os << " r" << instruction.a;
      }
      break;
    case Opcode::Branch:
      os << "branch r" << instruction.a << " ? b" << instruction.targetTrue << " : b"
         << instruction.targetFalse;
      break;
    case Opcode::Jump:
      os << "jump b" << instruction.targetTrue;
      break;
    case Opcode::Assume:
      os << "assume r" << instruction.a;
      break;
    case Opcode::Assert:
      os << "assert r" << instruction.a;
      break;
    case Opcode::Unreachable:
      os << "unreachable";
      break;
  }
  return os.str();
}

std::string dumpFunction(const IrModule &module, const IrFunction &function) {
  std::ostringstream os;
  os << "function " << function.name << "\n";
  for (size_t index = 0; index < function.blocks.size(); index++) {
    os << "b" << index << ":\n";
    for (const Instruction &instruction : function.blocks[index].instructions) {
      os << "  " << describeInstruction(module, instruction) << "\n";
    }
  }
  return os.str();
}

}
