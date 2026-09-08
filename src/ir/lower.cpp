#include "ir/lower.h"

#include <sstream>
#include <unordered_map>
#include <utility>
#include <vector>

#include "ir/lexer.h"
#include "ir/parser.h"
#include "solver/expr.h"
#include "util/support.h"

namespace symx {
namespace {

struct Value {
  int reg = -1;
  TypeRef type = nullptr;
};

struct Binding {
  int addressRegister = -1;
  TypeRef type = nullptr;
  bool isGlobal = false;
  int globalIndex = -1;
};

struct LoopTargets {
  int continueBlock = -1;
  int breakBlock = -1;
};

class Lowering {
 public:
  Lowering(const Program &program, IrModule &module) : program_(program), module_(module) {}

  bool run(std::string &error);

 private:
  void fail(const SourceLocation &location, const std::string &message);
  bool failed() const { return failed_; }

  int newBlock();
  void setBlock(int block);
  void ensureOpen();
  Instruction &emit(Instruction instruction);
  int newRegister(unsigned width);

  int emitConstant(uint64_t value, unsigned width, const SourceLocation &location);
  int emitBinary(IrBinary op, int a, int b, unsigned width, const SourceLocation &location);
  int emitCast(int source, unsigned sourceWidth, unsigned targetWidth, bool sourceSigned,
               const SourceLocation &location);
  int emitLoad(int address, unsigned width, const SourceLocation &location);
  void emitStore(int address, int value, unsigned width, const SourceLocation &location);
  int emitAlloca(uint64_t size, const SourceLocation &location);
  void emitJump(int target, const SourceLocation &location);
  void emitBranch(int condition, int trueBlock, int falseBlock, const SourceLocation &location);

  void pushScope();
  void popScope();
  void declare(const std::string &name, const Binding &binding);
  const Binding *lookup(const std::string &name) const;

  bool lowerFunctionBody(const FunctionDeclaration &declaration, IrFunction &function);
  void hoistAllocas(IrFunction &function);
  void lowerStatement(const Statement &statement);
  void lowerBlockStatement(const Statement &statement);

  Value lowerValue(const Expression &expression);
  Value lowerAddress(const Expression &expression);
  Value lowerCall(const Expression &expression);
  Value lowerLogical(const Expression &expression, bool isAnd);
  Value lowerConditional(const Expression &expression);
  Value lowerIncrement(const Expression &expression, bool increment, bool prefix);
  Value lowerAssign(const Expression &expression);
  Value lowerCompoundAssign(const Expression &expression);
  Value lowerBinary(const Expression &expression);

  int lowerCondition(const Expression &expression);
  Value convert(Value value, TypeRef target, const SourceLocation &location);
  Value promote(Value value, const SourceLocation &location);
  Value decay(Value value, const SourceLocation &location);
  TypeRef commonType(TypeRef left, TypeRef right) const;

  const Program &program_;
  IrModule &module_;
  IrFunction *function_ = nullptr;
  int currentBlock_ = -1;
  bool terminated_ = false;
  bool failed_ = false;
  std::string error_;
  std::vector<std::unordered_map<std::string, Binding>> scopes_;
  std::vector<LoopTargets> loops_;
  std::vector<TypeRef> globalTypes_;
};

void Lowering::fail(const SourceLocation &location, const std::string &message) {
  if (failed_) {
    return;
  }
  failed_ = true;
  std::ostringstream os;
  os << module_.fileName << ":" << location.line << ":" << location.column << ": " << message;
  error_ = os.str();
}

int Lowering::newBlock() {
  function_->blocks.push_back(BasicBlock());
  return static_cast<int>(function_->blocks.size()) - 1;
}

void Lowering::setBlock(int block) {
  currentBlock_ = block;
  terminated_ = false;
}

void Lowering::ensureOpen() {
  if (!terminated_) {
    return;
  }
  currentBlock_ = newBlock();
  terminated_ = false;
}

Instruction &Lowering::emit(Instruction instruction) {
  ensureOpen();
  module_.instructionCount++;
  BasicBlock &block = function_->blocks[currentBlock_];
  block.instructions.push_back(instruction);
  Opcode opcode = block.instructions.back().opcode;
  if (opcode == Opcode::Branch || opcode == Opcode::Jump || opcode == Opcode::Return ||
      opcode == Opcode::Unreachable) {
    terminated_ = true;
  }
  return block.instructions.back();
}

int Lowering::newRegister(unsigned width) {
  function_->registerWidths.push_back(width);
  return static_cast<int>(function_->registerWidths.size()) - 1;
}

int Lowering::emitConstant(uint64_t value, unsigned width, const SourceLocation &location) {
  Instruction instruction;
  instruction.opcode = Opcode::Constant;
  instruction.result = newRegister(width);
  instruction.width = width;
  instruction.immediate = value;
  instruction.location = location;
  return emit(instruction).result;
}

int Lowering::emitBinary(IrBinary op, int a, int b, unsigned width,
                         const SourceLocation &location) {
  Instruction instruction;
  instruction.opcode = Opcode::Binary;
  instruction.binary = op;
  instruction.a = a;
  instruction.b = b;
  instruction.width = width;
  instruction.result = newRegister(width);
  instruction.location = location;
  return emit(instruction).result;
}

int Lowering::emitCast(int source, unsigned sourceWidth, unsigned targetWidth, bool sourceSigned,
                       const SourceLocation &location) {
  if (sourceWidth == targetWidth) {
    return source;
  }
  Instruction instruction;
  instruction.opcode = Opcode::Cast;
  instruction.a = source;
  instruction.width = targetWidth;
  instruction.sourceSigned = sourceSigned;
  instruction.result = newRegister(targetWidth);
  instruction.location = location;
  return emit(instruction).result;
}

int Lowering::emitLoad(int address, unsigned width, const SourceLocation &location) {
  Instruction instruction;
  instruction.opcode = Opcode::Load;
  instruction.a = address;
  instruction.width = width;
  instruction.result = newRegister(width);
  instruction.location = location;
  return emit(instruction).result;
}

void Lowering::emitStore(int address, int value, unsigned width, const SourceLocation &location) {
  Instruction instruction;
  instruction.opcode = Opcode::Store;
  instruction.a = address;
  instruction.b = value;
  instruction.width = width;
  instruction.location = location;
  emit(instruction);
}

int Lowering::emitAlloca(uint64_t size, const SourceLocation &location) {
  Instruction instruction;
  instruction.opcode = Opcode::Alloca;
  instruction.immediate = size;
  instruction.width = 64;
  instruction.result = newRegister(64);
  instruction.location = location;
  return emit(instruction).result;
}

void Lowering::emitJump(int target, const SourceLocation &location) {
  Instruction instruction;
  instruction.opcode = Opcode::Jump;
  instruction.targetTrue = target;
  instruction.location = location;
  emit(instruction);
}

void Lowering::emitBranch(int condition, int trueBlock, int falseBlock,
                          const SourceLocation &location) {
  Instruction instruction;
  instruction.opcode = Opcode::Branch;
  instruction.a = condition;
  instruction.targetTrue = trueBlock;
  instruction.targetFalse = falseBlock;
  instruction.branchId = module_.branchCount++;
  instruction.location = location;
  emit(instruction);
}

void Lowering::pushScope() { scopes_.push_back({}); }

void Lowering::popScope() { scopes_.pop_back(); }

void Lowering::declare(const std::string &name, const Binding &binding) {
  scopes_.back()[name] = binding;
}

const Binding *Lowering::lookup(const std::string &name) const {
  for (size_t i = scopes_.size(); i > 0; i--) {
    auto found = scopes_[i - 1].find(name);
    if (found != scopes_[i - 1].end()) {
      return &found->second;
    }
  }
  return nullptr;
}

Value Lowering::decay(Value value, const SourceLocation &location) {
  (void)location;
  if (value.type != nullptr && isArrayType(value.type)) {
    Value result;
    result.reg = value.reg;
    result.type = pointerTo(value.type->element);
    return result;
  }
  return value;
}

Value Lowering::promote(Value value, const SourceLocation &location) {
  value = decay(value, location);
  if (value.type == nullptr || !isIntegerType(value.type)) {
    return value;
  }
  if (value.type->bits >= 32) {
    return value;
  }
  Value result;
  result.type = integerType(32, true);
  result.reg = emitCast(value.reg, value.type->bits, 32, value.type->isSigned, location);
  return result;
}

TypeRef Lowering::commonType(TypeRef left, TypeRef right) const {
  unsigned bits = left->bits > right->bits ? left->bits : right->bits;
  bool isSigned = true;
  if (left->bits == right->bits) {
    isSigned = left->isSigned && right->isSigned;
  } else if (left->bits > right->bits) {
    isSigned = left->isSigned;
  } else {
    isSigned = right->isSigned;
  }
  return integerType(bits, isSigned);
}

Value Lowering::convert(Value value, TypeRef target, const SourceLocation &location) {
  value = decay(value, location);
  if (value.type == target) {
    return value;
  }
  unsigned sourceWidth = typeBitWidth(value.type);
  unsigned targetWidth = typeBitWidth(target);
  bool sourceSigned = isIntegerType(value.type) ? value.type->isSigned : false;
  Value result;
  result.type = target;
  result.reg = emitCast(value.reg, sourceWidth, targetWidth, sourceSigned, location);
  return result;
}

int Lowering::lowerCondition(const Expression &expression) {
  Value value = decay(lowerValue(expression), expression.location);
  if (failed()) {
    return emitConstant(0, 1, expression.location);
  }
  unsigned width = typeBitWidth(value.type);
  int zero = emitConstant(0, width, expression.location);
  return emitBinary(IrBinary::NotEqual, value.reg, zero, 1, expression.location);
}

Value Lowering::lowerAddress(const Expression &expression) {
  Value result;
  switch (expression.kind) {
    case ExpressionKind::Identifier: {
      const Binding *binding = lookup(expression.name);
      if (binding == nullptr) {
        fail(expression.location, "unknown identifier " + expression.name);
        result.reg = emitConstant(0, 64, expression.location);
        result.type = integerType(32, true);
        return result;
      }
      if (binding->isGlobal) {
        Instruction instruction;
        instruction.opcode = Opcode::GlobalAddress;
        instruction.immediate = static_cast<uint64_t>(binding->globalIndex);
        instruction.width = 64;
        instruction.result = newRegister(64);
        instruction.location = expression.location;
        result.reg = emit(instruction).result;
      } else {
        result.reg = binding->addressRegister;
      }
      result.type = binding->type;
      return result;
    }
    case ExpressionKind::Index: {
      Value base = decay(lowerValue(*expression.a), expression.location);
      Value index = promote(lowerValue(*expression.b), expression.location);
      if (failed()) {
        return result;
      }
      if (!isPointerType(base.type)) {
        fail(expression.location, "subscripted value is not an array or pointer");
        return result;
      }
      TypeRef element = base.type->element;
      uint64_t elementSize = typeSizeInBytes(element);
      Value indexAsLong = convert(index, integerType(64, true), expression.location);
      int scale = emitConstant(elementSize, 64, expression.location);
      int offset = emitBinary(IrBinary::Mul, indexAsLong.reg, scale, 64, expression.location);
      result.reg = emitBinary(IrBinary::Add, base.reg, offset, 64, expression.location);
      result.type = element;
      return result;
    }
    case ExpressionKind::Unary: {
      if (expression.unaryOp == UnaryOp::Dereference) {
        Value pointer = decay(lowerValue(*expression.a), expression.location);
        if (failed()) {
          return result;
        }
        if (!isPointerType(pointer.type)) {
          fail(expression.location, "dereference of a non pointer value");
          return result;
        }
        result.reg = pointer.reg;
        result.type = pointer.type->element;
        return result;
      }
      break;
    }
    default:
      break;
  }
  fail(expression.location, "expression is not assignable");
  return result;
}

Value Lowering::lowerAssign(const Expression &expression) {
  Value address = lowerAddress(*expression.a);
  Value value = lowerValue(*expression.b);
  if (failed()) {
    return value;
  }
  if (isArrayType(address.type)) {
    fail(expression.location, "cannot assign to an array");
    return value;
  }
  Value converted = convert(value, address.type, expression.location);
  emitStore(address.reg, converted.reg, typeBitWidth(address.type), expression.location);
  return converted;
}

Value Lowering::lowerCompoundAssign(const Expression &expression) {
  Value address = lowerAddress(*expression.a);
  if (failed()) {
    return Value();
  }
  Value current;
  current.type = address.type;
  current.reg = emitLoad(address.reg, typeBitWidth(address.type), expression.location);
  Expression synthetic;
  synthetic.kind = ExpressionKind::Binary;
  synthetic.binaryOp = expression.binaryOp;
  synthetic.location = expression.location;
  Value right = lowerValue(*expression.b);
  if (failed()) {
    return Value();
  }
  Value combined;
  if (isPointerType(address.type) &&
      (expression.binaryOp == BinaryOp::Add || expression.binaryOp == BinaryOp::Subtract)) {
    uint64_t elementSize = typeSizeInBytes(address.type->element);
    Value indexAsLong =
        convert(promote(right, expression.location), integerType(64, true), expression.location);
    int scale = emitConstant(elementSize, 64, expression.location);
    int offset = emitBinary(IrBinary::Mul, indexAsLong.reg, scale, 64, expression.location);
    combined.reg = emitBinary(expression.binaryOp == BinaryOp::Add ? IrBinary::Add : IrBinary::Sub,
                              current.reg, offset, 64, expression.location);
    combined.type = address.type;
  } else {
    Value left = promote(current, expression.location);
    Value rightPromoted = promote(right, expression.location);
    TypeRef target = commonType(left.type, rightPromoted.type);
    left = convert(left, target, expression.location);
    rightPromoted = convert(rightPromoted, target, expression.location);
    IrBinary op = IrBinary::Add;
    switch (expression.binaryOp) {
      case BinaryOp::Add:
        op = IrBinary::Add;
        break;
      case BinaryOp::Subtract:
        op = IrBinary::Sub;
        break;
      case BinaryOp::Multiply:
        op = IrBinary::Mul;
        break;
      case BinaryOp::Divide:
        op = target->isSigned ? IrBinary::SDiv : IrBinary::UDiv;
        break;
      case BinaryOp::Modulo:
        op = target->isSigned ? IrBinary::SRem : IrBinary::URem;
        break;
      case BinaryOp::ShiftLeft:
        op = IrBinary::Shl;
        break;
      case BinaryOp::ShiftRight:
        op = target->isSigned ? IrBinary::AShr : IrBinary::LShr;
        break;
      case BinaryOp::BitAnd:
        op = IrBinary::And;
        break;
      case BinaryOp::BitOr:
        op = IrBinary::Or;
        break;
      case BinaryOp::BitXor:
        op = IrBinary::Xor;
        break;
      default:
        fail(expression.location, "unsupported compound assignment");
        return Value();
    }
    combined.reg =
        emitBinary(op, left.reg, rightPromoted.reg, typeBitWidth(target), expression.location);
    combined.type = target;
  }
  Value stored = convert(combined, address.type, expression.location);
  emitStore(address.reg, stored.reg, typeBitWidth(address.type), expression.location);
  return stored;
}

Value Lowering::lowerIncrement(const Expression &expression, bool increment, bool prefix) {
  Value address = lowerAddress(*expression.a);
  if (failed()) {
    return Value();
  }
  Value old;
  old.type = address.type;
  old.reg = emitLoad(address.reg, typeBitWidth(address.type), expression.location);
  uint64_t step = 1;
  if (isPointerType(address.type)) {
    step = typeSizeInBytes(address.type->element);
  }
  int stepRegister = emitConstant(step, typeBitWidth(address.type), expression.location);
  int updated = emitBinary(increment ? IrBinary::Add : IrBinary::Sub, old.reg, stepRegister,
                           typeBitWidth(address.type), expression.location);
  emitStore(address.reg, updated, typeBitWidth(address.type), expression.location);
  Value result;
  result.type = address.type;
  result.reg = prefix ? updated : old.reg;
  return result;
}

Value Lowering::lowerLogical(const Expression &expression, bool isAnd) {
  int slot = emitAlloca(4, expression.location);
  int leftCondition = lowerCondition(*expression.a);
  int rightBlock = newBlock();
  int shortBlock = newBlock();
  int joinBlock = newBlock();
  if (isAnd) {
    emitBranch(leftCondition, rightBlock, shortBlock, expression.location);
  } else {
    emitBranch(leftCondition, shortBlock, rightBlock, expression.location);
  }
  setBlock(rightBlock);
  int rightCondition = lowerCondition(*expression.b);
  int rightValue = emitCast(rightCondition, 1, 32, false, expression.location);
  emitStore(slot, rightValue, 32, expression.location);
  emitJump(joinBlock, expression.location);
  setBlock(shortBlock);
  int shortValue = emitConstant(isAnd ? 0 : 1, 32, expression.location);
  emitStore(slot, shortValue, 32, expression.location);
  emitJump(joinBlock, expression.location);
  setBlock(joinBlock);
  Value result;
  result.type = integerType(32, true);
  result.reg = emitLoad(slot, 32, expression.location);
  return result;
}

Value Lowering::lowerConditional(const Expression &expression) {
  int slot = emitAlloca(8, expression.location);
  int condition = lowerCondition(*expression.a);
  int thenBlock = newBlock();
  int elseBlock = newBlock();
  int joinBlock = newBlock();
  emitBranch(condition, thenBlock, elseBlock, expression.location);

  setBlock(thenBlock);
  Value thenValue = decay(lowerValue(*expression.b), expression.location);
  int thenEnd = currentBlock_;
  setBlock(elseBlock);
  Value elseValue = decay(lowerValue(*expression.c), expression.location);
  int elseEnd = currentBlock_;
  if (failed()) {
    return thenValue;
  }
  TypeRef target = thenValue.type;
  if (isIntegerType(thenValue.type) && isIntegerType(elseValue.type)) {
    TypeRef left = thenValue.type->bits < 32 ? integerType(32, true) : thenValue.type;
    TypeRef right = elseValue.type->bits < 32 ? integerType(32, true) : elseValue.type;
    target = commonType(left, right);
  }

  currentBlock_ = thenEnd;
  terminated_ = false;
  Value thenConverted = convert(thenValue, target, expression.location);
  emitStore(slot, thenConverted.reg, typeBitWidth(target), expression.location);
  emitJump(joinBlock, expression.location);

  currentBlock_ = elseEnd;
  terminated_ = false;
  Value elseConverted = convert(elseValue, target, expression.location);
  emitStore(slot, elseConverted.reg, typeBitWidth(target), expression.location);
  emitJump(joinBlock, expression.location);

  setBlock(joinBlock);
  Value result;
  result.type = target;
  result.reg = emitLoad(slot, typeBitWidth(target), expression.location);
  return result;
}

Value Lowering::lowerCall(const Expression &expression) {
  Value result;
  const std::string &name = expression.name;
  if (name == "assert" || name == "symx_assert") {
    if (expression.arguments.size() != 1) {
      fail(expression.location, "assert takes one argument");
      return result;
    }
    int condition = lowerCondition(*expression.arguments[0]);
    Instruction instruction;
    instruction.opcode = Opcode::Assert;
    instruction.a = condition;
    instruction.location = expression.location;
    emit(instruction);
    result.type = integerType(32, true);
    result.reg = emitConstant(0, 32, expression.location);
    return result;
  }
  if (name == "symx_assume") {
    if (expression.arguments.size() != 1) {
      fail(expression.location, "symx_assume takes one argument");
      return result;
    }
    int condition = lowerCondition(*expression.arguments[0]);
    Instruction instruction;
    instruction.opcode = Opcode::Assume;
    instruction.a = condition;
    instruction.location = expression.location;
    emit(instruction);
    result.type = integerType(32, true);
    result.reg = emitConstant(0, 32, expression.location);
    return result;
  }
  if (name == "abort") {
    int condition = emitConstant(0, 1, expression.location);
    Instruction instruction;
    instruction.opcode = Opcode::Assert;
    instruction.a = condition;
    instruction.location = expression.location;
    emit(instruction);
    result.type = integerType(32, true);
    result.reg = emitConstant(0, 32, expression.location);
    return result;
  }
  auto found = module_.functionIndex.find(name);
  if (found == module_.functionIndex.end()) {
    fail(expression.location, "call to undefined function " + name);
    return result;
  }
  const IrFunction &callee = module_.functions[found->second];
  if (callee.parameterTypes.size() != expression.arguments.size()) {
    fail(expression.location, "wrong number of arguments in call to " + name);
    return result;
  }
  std::vector<int> argumentRegisters;
  for (size_t i = 0; i < expression.arguments.size(); i++) {
    Value argument = lowerValue(*expression.arguments[i]);
    if (failed()) {
      return result;
    }
    Value converted = convert(argument, callee.parameterTypes[i], expression.location);
    argumentRegisters.push_back(converted.reg);
  }
  Instruction instruction;
  instruction.opcode = Opcode::Call;
  instruction.immediate = static_cast<uint64_t>(found->second);
  instruction.arguments = argumentRegisters;
  instruction.location = expression.location;
  bool returnsValue = callee.returnType->kind != TypeKind::Void;
  unsigned width = returnsValue ? typeBitWidth(callee.returnType) : 32;
  instruction.width = width;
  instruction.result = newRegister(width);
  int resultRegister = emit(instruction).result;
  result.reg = resultRegister;
  result.type = returnsValue ? callee.returnType : integerType(32, true);
  return result;
}

Value Lowering::lowerBinary(const Expression &expression) {
  Value result;
  if (expression.binaryOp == BinaryOp::LogicalAnd) {
    return lowerLogical(expression, true);
  }
  if (expression.binaryOp == BinaryOp::LogicalOr) {
    return lowerLogical(expression, false);
  }
  if (expression.binaryOp == BinaryOp::Comma) {
    lowerValue(*expression.a);
    return lowerValue(*expression.b);
  }
  Value left = decay(lowerValue(*expression.a), expression.location);
  Value right = decay(lowerValue(*expression.b), expression.location);
  if (failed()) {
    return result;
  }
  bool leftIsPointer = isPointerType(left.type);
  bool rightIsPointer = isPointerType(right.type);
  if (leftIsPointer || rightIsPointer) {
    if (expression.binaryOp == BinaryOp::Add || expression.binaryOp == BinaryOp::Subtract) {
      if (leftIsPointer && rightIsPointer && expression.binaryOp == BinaryOp::Subtract) {
        uint64_t elementSize = typeSizeInBytes(left.type->element);
        int difference = emitBinary(IrBinary::Sub, left.reg, right.reg, 64, expression.location);
        int scale = emitConstant(elementSize == 0 ? 1 : elementSize, 64, expression.location);
        result.reg = emitBinary(IrBinary::SDiv, difference, scale, 64, expression.location);
        result.type = integerType(64, true);
        return result;
      }
      Value pointer = leftIsPointer ? left : right;
      Value index = leftIsPointer ? right : left;
      uint64_t elementSize = typeSizeInBytes(pointer.type->element);
      Value indexAsLong =
          convert(promote(index, expression.location), integerType(64, true), expression.location);
      int scale = emitConstant(elementSize, 64, expression.location);
      int offset = emitBinary(IrBinary::Mul, indexAsLong.reg, scale, 64, expression.location);
      result.reg = emitBinary(expression.binaryOp == BinaryOp::Add ? IrBinary::Add : IrBinary::Sub,
                              pointer.reg, offset, 64, expression.location);
      result.type = pointer.type;
      return result;
    }
    Value leftLong = convert(left, integerType(64, false), expression.location);
    Value rightLong = convert(right, integerType(64, false), expression.location);
    IrBinary op = IrBinary::Equal;
    switch (expression.binaryOp) {
      case BinaryOp::Equal:
        op = IrBinary::Equal;
        break;
      case BinaryOp::NotEqual:
        op = IrBinary::NotEqual;
        break;
      case BinaryOp::Less:
        op = IrBinary::UnsignedLess;
        break;
      case BinaryOp::LessEqual:
        op = IrBinary::UnsignedLessEqual;
        break;
      case BinaryOp::Greater:
        op = IrBinary::UnsignedLess;
        break;
      case BinaryOp::GreaterEqual:
        op = IrBinary::UnsignedLessEqual;
        break;
      default:
        fail(expression.location, "unsupported pointer operation");
        return result;
    }
    bool swapped =
        expression.binaryOp == BinaryOp::Greater || expression.binaryOp == BinaryOp::GreaterEqual;
    int comparison = swapped ? emitBinary(op, rightLong.reg, leftLong.reg, 1, expression.location)
                             : emitBinary(op, leftLong.reg, rightLong.reg, 1, expression.location);
    result.reg = emitCast(comparison, 1, 32, false, expression.location);
    result.type = integerType(32, true);
    return result;
  }

  Value leftPromoted = promote(left, expression.location);
  Value rightPromoted = promote(right, expression.location);
  if (!isIntegerType(leftPromoted.type) || !isIntegerType(rightPromoted.type)) {
    fail(expression.location, "operands must be integers");
    return result;
  }
  bool isComparison =
      expression.binaryOp == BinaryOp::Less || expression.binaryOp == BinaryOp::Greater ||
      expression.binaryOp == BinaryOp::LessEqual || expression.binaryOp == BinaryOp::GreaterEqual ||
      expression.binaryOp == BinaryOp::Equal || expression.binaryOp == BinaryOp::NotEqual;
  bool isShift =
      expression.binaryOp == BinaryOp::ShiftLeft || expression.binaryOp == BinaryOp::ShiftRight;
  TypeRef target = isShift ? leftPromoted.type : commonType(leftPromoted.type, rightPromoted.type);
  Value a = convert(leftPromoted, target, expression.location);
  Value b = isShift ? convert(rightPromoted, target, expression.location)
                    : convert(rightPromoted, target, expression.location);
  unsigned width = typeBitWidth(target);
  IrBinary op = IrBinary::Add;
  bool swapOperands = false;
  switch (expression.binaryOp) {
    case BinaryOp::Add:
      op = IrBinary::Add;
      break;
    case BinaryOp::Subtract:
      op = IrBinary::Sub;
      break;
    case BinaryOp::Multiply:
      op = IrBinary::Mul;
      break;
    case BinaryOp::Divide:
      op = target->isSigned ? IrBinary::SDiv : IrBinary::UDiv;
      break;
    case BinaryOp::Modulo:
      op = target->isSigned ? IrBinary::SRem : IrBinary::URem;
      break;
    case BinaryOp::ShiftLeft:
      op = IrBinary::Shl;
      break;
    case BinaryOp::ShiftRight:
      op = target->isSigned ? IrBinary::AShr : IrBinary::LShr;
      break;
    case BinaryOp::BitAnd:
      op = IrBinary::And;
      break;
    case BinaryOp::BitOr:
      op = IrBinary::Or;
      break;
    case BinaryOp::BitXor:
      op = IrBinary::Xor;
      break;
    case BinaryOp::Equal:
      op = IrBinary::Equal;
      break;
    case BinaryOp::NotEqual:
      op = IrBinary::NotEqual;
      break;
    case BinaryOp::Less:
      op = target->isSigned ? IrBinary::SignedLess : IrBinary::UnsignedLess;
      break;
    case BinaryOp::LessEqual:
      op = target->isSigned ? IrBinary::SignedLessEqual : IrBinary::UnsignedLessEqual;
      break;
    case BinaryOp::Greater:
      op = target->isSigned ? IrBinary::SignedLess : IrBinary::UnsignedLess;
      swapOperands = true;
      break;
    case BinaryOp::GreaterEqual:
      op = target->isSigned ? IrBinary::SignedLessEqual : IrBinary::UnsignedLessEqual;
      swapOperands = true;
      break;
    default:
      fail(expression.location, "unsupported operator");
      return result;
  }
  if (isComparison) {
    int comparison = swapOperands ? emitBinary(op, b.reg, a.reg, 1, expression.location)
                                  : emitBinary(op, a.reg, b.reg, 1, expression.location);
    result.reg = emitCast(comparison, 1, 32, false, expression.location);
    result.type = integerType(32, true);
    return result;
  }
  result.reg = emitBinary(op, a.reg, b.reg, width, expression.location);
  result.type = target;
  return result;
}

Value Lowering::lowerValue(const Expression &expression) {
  Value result;
  if (failed()) {
    return result;
  }
  switch (expression.kind) {
    case ExpressionKind::IntLiteral: {
      unsigned width = expression.literalLong || expression.literal > 0xffffffffULL ? 64 : 32;
      bool isSigned = !expression.literalUnsigned;
      if (isSigned && width == 32 && expression.literal > 0x7fffffffULL) {
        width = 64;
      }
      result.type = integerType(width, isSigned);
      result.reg = emitConstant(expression.literal, width, expression.location);
      return result;
    }
    case ExpressionKind::Identifier: {
      Value address = lowerAddress(expression);
      if (failed()) {
        return result;
      }
      if (isArrayType(address.type)) {
        result.reg = address.reg;
        result.type = address.type;
        return decay(result, expression.location);
      }
      result.type = address.type;
      result.reg = emitLoad(address.reg, typeBitWidth(address.type), expression.location);
      return result;
    }
    case ExpressionKind::Index: {
      Value address = lowerAddress(expression);
      if (failed()) {
        return result;
      }
      if (isArrayType(address.type)) {
        result.reg = address.reg;
        result.type = address.type;
        return decay(result, expression.location);
      }
      result.type = address.type;
      result.reg = emitLoad(address.reg, typeBitWidth(address.type), expression.location);
      return result;
    }
    case ExpressionKind::Unary: {
      switch (expression.unaryOp) {
        case UnaryOp::AddressOf: {
          Value address = lowerAddress(*expression.a);
          if (failed()) {
            return result;
          }
          result.reg = address.reg;
          result.type = pointerTo(address.type);
          return result;
        }
        case UnaryOp::Dereference: {
          Value address = lowerAddress(expression);
          if (failed()) {
            return result;
          }
          if (isArrayType(address.type)) {
            result.reg = address.reg;
            result.type = address.type;
            return decay(result, expression.location);
          }
          result.type = address.type;
          result.reg = emitLoad(address.reg, typeBitWidth(address.type), expression.location);
          return result;
        }
        case UnaryOp::PreIncrement:
          return lowerIncrement(expression, true, true);
        case UnaryOp::PreDecrement:
          return lowerIncrement(expression, false, true);
        case UnaryOp::PostIncrement:
          return lowerIncrement(expression, true, false);
        case UnaryOp::PostDecrement:
          return lowerIncrement(expression, false, false);
        case UnaryOp::Plus:
          return promote(lowerValue(*expression.a), expression.location);
        case UnaryOp::Minus: {
          Value operand = promote(lowerValue(*expression.a), expression.location);
          if (failed()) {
            return result;
          }
          unsigned width = typeBitWidth(operand.type);
          int zero = emitConstant(0, width, expression.location);
          result.reg = emitBinary(IrBinary::Sub, zero, operand.reg, width, expression.location);
          result.type = operand.type;
          return result;
        }
        case UnaryOp::BitNot: {
          Value operand = promote(lowerValue(*expression.a), expression.location);
          if (failed()) {
            return result;
          }
          unsigned width = typeBitWidth(operand.type);
          int ones = emitConstant(maskFor(width), width, expression.location);
          result.reg = emitBinary(IrBinary::Xor, operand.reg, ones, width, expression.location);
          result.type = operand.type;
          return result;
        }
        case UnaryOp::LogicalNot: {
          Value operand = decay(lowerValue(*expression.a), expression.location);
          if (failed()) {
            return result;
          }
          unsigned width = typeBitWidth(operand.type);
          int zero = emitConstant(0, width, expression.location);
          int comparison = emitBinary(IrBinary::Equal, operand.reg, zero, 1, expression.location);
          result.reg = emitCast(comparison, 1, 32, false, expression.location);
          result.type = integerType(32, true);
          return result;
        }
      }
      break;
    }
    case ExpressionKind::Binary:
      return lowerBinary(expression);
    case ExpressionKind::Assign:
      return lowerAssign(expression);
    case ExpressionKind::CompoundAssign:
      return lowerCompoundAssign(expression);
    case ExpressionKind::Call:
      return lowerCall(expression);
    case ExpressionKind::Conditional:
      return lowerConditional(expression);
    case ExpressionKind::Cast: {
      Value operand = lowerValue(*expression.a);
      if (failed()) {
        return result;
      }
      if (expression.typeOperand->kind == TypeKind::Void) {
        result.type = integerType(32, true);
        result.reg = emitConstant(0, 32, expression.location);
        return result;
      }
      return convert(operand, expression.typeOperand, expression.location);
    }
    case ExpressionKind::SizeOfType: {
      uint64_t size = 0;
      if (expression.typeOperand != nullptr) {
        size = typeSizeInBytes(expression.typeOperand);
      } else {
        Value operand = lowerValue(*expression.a);
        size = operand.type == nullptr ? 0 : typeSizeInBytes(operand.type);
      }
      result.type = integerType(64, false);
      result.reg = emitConstant(size, 64, expression.location);
      return result;
    }
  }
  fail(expression.location, "unsupported expression");
  return result;
}

void Lowering::lowerBlockStatement(const Statement &statement) {
  pushScope();
  for (const StatementPtr &child : statement.body) {
    lowerStatement(*child);
    if (failed()) {
      break;
    }
  }
  popScope();
}

void Lowering::lowerStatement(const Statement &statement) {
  if (failed()) {
    return;
  }
  switch (statement.kind) {
    case StatementKind::Compound:
      lowerBlockStatement(statement);
      return;
    case StatementKind::Empty:
      return;
    case StatementKind::Declaration: {
      uint64_t size = typeSizeInBytes(statement.declarationType);
      int address = emitAlloca(size == 0 ? 1 : size, statement.location);
      Binding binding;
      binding.addressRegister = address;
      binding.type = statement.declarationType;
      declare(statement.declarationName, binding);
      if (statement.initializer) {
        Value value = lowerValue(*statement.initializer);
        if (failed()) {
          return;
        }
        Value converted = convert(value, statement.declarationType, statement.location);
        emitStore(address, converted.reg, typeBitWidth(statement.declarationType),
                  statement.location);
      } else if (!statement.initializerList.empty()) {
        if (!isArrayType(statement.declarationType)) {
          fail(statement.location, "brace initializer requires an array");
          return;
        }
        TypeRef element = statement.declarationType->element;
        uint64_t elementSize = typeSizeInBytes(element);
        for (size_t i = 0; i < statement.initializerList.size(); i++) {
          Value value = lowerValue(*statement.initializerList[i]);
          if (failed()) {
            return;
          }
          Value converted = convert(value, element, statement.location);
          int offset = emitConstant(i * elementSize, 64, statement.location);
          int target = emitBinary(IrBinary::Add, address, offset, 64, statement.location);
          emitStore(target, converted.reg, typeBitWidth(element), statement.location);
        }
      }
      return;
    }
    case StatementKind::ExpressionStatement:
      lowerValue(*statement.expression);
      return;
    case StatementKind::If: {
      int condition = lowerCondition(*statement.condition);
      int thenBlock = newBlock();
      int elseBlock = statement.elseBranch ? newBlock() : -1;
      int joinBlock = newBlock();
      emitBranch(condition, thenBlock, elseBlock >= 0 ? elseBlock : joinBlock, statement.location);
      setBlock(thenBlock);
      lowerStatement(*statement.thenBranch);
      if (!terminated_) {
        emitJump(joinBlock, statement.location);
      }
      if (elseBlock >= 0) {
        setBlock(elseBlock);
        lowerStatement(*statement.elseBranch);
        if (!terminated_) {
          emitJump(joinBlock, statement.location);
        }
      }
      setBlock(joinBlock);
      return;
    }
    case StatementKind::While: {
      int headerBlock = newBlock();
      int bodyBlock = newBlock();
      int exitBlock = newBlock();
      emitJump(headerBlock, statement.location);
      setBlock(headerBlock);
      int condition = lowerCondition(*statement.condition);
      emitBranch(condition, bodyBlock, exitBlock, statement.location);
      setBlock(bodyBlock);
      loops_.push_back({headerBlock, exitBlock});
      lowerStatement(*statement.loopBody);
      loops_.pop_back();
      if (!terminated_) {
        emitJump(headerBlock, statement.location);
      }
      setBlock(exitBlock);
      return;
    }
    case StatementKind::DoWhile: {
      int bodyBlock = newBlock();
      int conditionBlock = newBlock();
      int exitBlock = newBlock();
      emitJump(bodyBlock, statement.location);
      setBlock(bodyBlock);
      loops_.push_back({conditionBlock, exitBlock});
      lowerStatement(*statement.loopBody);
      loops_.pop_back();
      if (!terminated_) {
        emitJump(conditionBlock, statement.location);
      }
      setBlock(conditionBlock);
      int condition = lowerCondition(*statement.condition);
      emitBranch(condition, bodyBlock, exitBlock, statement.location);
      setBlock(exitBlock);
      return;
    }
    case StatementKind::For: {
      pushScope();
      if (statement.initializerStatement) {
        lowerStatement(*statement.initializerStatement);
      }
      int headerBlock = newBlock();
      int bodyBlock = newBlock();
      int stepBlock = newBlock();
      int exitBlock = newBlock();
      emitJump(headerBlock, statement.location);
      setBlock(headerBlock);
      if (statement.condition) {
        int condition = lowerCondition(*statement.condition);
        emitBranch(condition, bodyBlock, exitBlock, statement.location);
      } else {
        emitJump(bodyBlock, statement.location);
      }
      setBlock(bodyBlock);
      loops_.push_back({stepBlock, exitBlock});
      lowerStatement(*statement.loopBody);
      loops_.pop_back();
      if (!terminated_) {
        emitJump(stepBlock, statement.location);
      }
      setBlock(stepBlock);
      if (statement.step) {
        lowerValue(*statement.step);
      }
      emitJump(headerBlock, statement.location);
      setBlock(exitBlock);
      popScope();
      return;
    }
    case StatementKind::Return: {
      Instruction instruction;
      instruction.opcode = Opcode::Return;
      instruction.location = statement.location;
      if (statement.expression) {
        Value value = lowerValue(*statement.expression);
        if (failed()) {
          return;
        }
        Value converted = convert(value, function_->returnType, statement.location);
        instruction.a = converted.reg;
        instruction.width = typeBitWidth(function_->returnType);
      }
      emit(instruction);
      return;
    }
    case StatementKind::Break: {
      if (loops_.empty()) {
        fail(statement.location, "break outside of a loop");
        return;
      }
      emitJump(loops_.back().breakBlock, statement.location);
      return;
    }
    case StatementKind::Continue: {
      if (loops_.empty()) {
        fail(statement.location, "continue outside of a loop");
        return;
      }
      emitJump(loops_.back().continueBlock, statement.location);
      return;
    }
  }
}

bool Lowering::lowerFunctionBody(const FunctionDeclaration &declaration, IrFunction &function) {
  function_ = &function;
  function.blocks.clear();
  function.registerWidths.clear();
  function.parameterRegisters.clear();
  currentBlock_ = newBlock();
  terminated_ = false;
  scopes_.clear();
  pushScope();
  for (size_t i = 0; i < declaration.parameters.size(); i++) {
    TypeRef type = declaration.parameters[i].type;
    int incoming = newRegister(typeBitWidth(type));
    function.parameterRegisters.push_back(incoming);
    int address = emitAlloca(typeSizeInBytes(type), declaration.location);
    emitStore(address, incoming, typeBitWidth(type), declaration.location);
    Binding binding;
    binding.addressRegister = address;
    binding.type = type;
    declare(declaration.parameters[i].name, binding);
  }
  for (const auto &entry : module_.globalIndex) {
    Binding binding;
    binding.isGlobal = true;
    binding.globalIndex = entry.second;
    binding.type = globalTypes_[entry.second];
    scopes_[0][entry.first] = binding;
  }
  lowerStatement(*declaration.body);
  if (!terminated_) {
    Instruction instruction;
    instruction.opcode = Opcode::Return;
    instruction.location = declaration.location;
    if (function.returnType->kind != TypeKind::Void) {
      instruction.a = emitConstant(0, typeBitWidth(function.returnType), declaration.location);
      instruction.width = typeBitWidth(function.returnType);
    }
    emit(instruction);
  }
  popScope();
  hoistAllocas(function);
  function_ = nullptr;
  return !failed_;
}

void Lowering::hoistAllocas(IrFunction &function) {
  std::vector<Instruction> allocas;
  for (BasicBlock &block : function.blocks) {
    std::vector<Instruction> kept;
    kept.reserve(block.instructions.size());
    for (Instruction &instruction : block.instructions) {
      if (instruction.opcode == Opcode::Alloca) {
        allocas.push_back(instruction);
      } else {
        kept.push_back(instruction);
      }
    }
    block.instructions.swap(kept);
  }
  if (allocas.empty()) {
    return;
  }
  std::vector<Instruction> &entry = function.blocks[0].instructions;
  entry.insert(entry.begin(), allocas.begin(), allocas.end());
}

bool Lowering::run(std::string &error) {
  module_.fileName = program_.fileName;
  for (const GlobalDeclaration &global : program_.globals) {
    IrGlobal target;
    target.name = global.name;
    target.size = typeSizeInBytes(global.type);
    target.initialBytes.assign(target.size, 0);
    TypeRef elementType = isArrayType(global.type) ? global.type->element : global.type;
    uint64_t elementSize = typeSizeInBytes(elementType);
    for (size_t i = 0; i < global.initialValues.size(); i++) {
      uint64_t value = global.initialValues[i];
      for (uint64_t byte = 0; byte < elementSize; byte++) {
        uint64_t position = i * elementSize + byte;
        if (position < target.initialBytes.size()) {
          target.initialBytes[position] = static_cast<uint8_t>((value >> (8 * byte)) & 0xff);
        }
      }
    }
    module_.globalIndex[global.name] = static_cast<int>(module_.globals.size());
    globalTypes_.push_back(global.type);
    module_.globals.push_back(target);
  }
  for (const FunctionDeclaration &declaration : program_.functions) {
    if (module_.functionIndex.count(declaration.name) != 0) {
      fail(declaration.location, "duplicate definition of " + declaration.name);
      error = error_;
      return false;
    }
    IrFunction function;
    function.name = declaration.name;
    function.returnType = declaration.returnType;
    function.location = declaration.location;
    for (const Parameter &parameter : declaration.parameters) {
      function.parameterTypes.push_back(parameter.type);
      function.parameterNames.push_back(parameter.name);
    }
    module_.functionIndex[declaration.name] = static_cast<int>(module_.functions.size());
    module_.functions.push_back(std::move(function));
  }
  for (size_t i = 0; i < program_.functions.size(); i++) {
    if (!lowerFunctionBody(program_.functions[i], module_.functions[i])) {
      error = error_;
      return false;
    }
  }
  return true;
}

}

bool lowerProgram(const Program &program, IrModule &module, std::string &error) {
  Lowering lowering(program, module);
  return lowering.run(error);
}

bool compileSource(const std::string &source, const std::string &fileName, IrModule &module,
                   std::string &error) {
  Lexer lexer(source, fileName);
  if (!lexer.tokenize(error)) {
    return false;
  }
  Parser parser(lexer.tokens(), fileName);
  Program program;
  if (!parser.parseProgram(program, error)) {
    return false;
  }
  return lowerProgram(program, module, error);
}

}
