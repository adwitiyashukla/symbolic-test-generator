#ifndef SYMX_IR_AST_H
#define SYMX_IR_AST_H

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "ir/lexer.h"

namespace symx {

enum class TypeKind { Void, Integer, Pointer, Array };

struct Type;
using TypeRef = const Type *;

struct Type {
  TypeKind kind = TypeKind::Void;
  unsigned bits = 0;
  bool isSigned = false;
  TypeRef element = nullptr;
  uint64_t count = 0;
};

TypeRef voidType();
TypeRef integerType(unsigned bits, bool isSigned);
TypeRef boolType();
TypeRef pointerTo(TypeRef element);
TypeRef arrayOf(TypeRef element, uint64_t count);
uint64_t typeSizeInBytes(TypeRef type);
unsigned typeBitWidth(TypeRef type);
bool isIntegerType(TypeRef type);
bool isPointerType(TypeRef type);
bool isArrayType(TypeRef type);
bool isScalarType(TypeRef type);
TypeRef decayType(TypeRef type);
std::string typeName(TypeRef type);

enum class UnaryOp {
  Plus,
  Minus,
  LogicalNot,
  BitNot,
  Dereference,
  AddressOf,
  PreIncrement,
  PreDecrement,
  PostIncrement,
  PostDecrement
};

enum class BinaryOp {
  Add,
  Subtract,
  Multiply,
  Divide,
  Modulo,
  ShiftLeft,
  ShiftRight,
  Less,
  Greater,
  LessEqual,
  GreaterEqual,
  Equal,
  NotEqual,
  BitAnd,
  BitOr,
  BitXor,
  LogicalAnd,
  LogicalOr,
  Comma
};

enum class ExpressionKind {
  IntLiteral,
  Identifier,
  Unary,
  Binary,
  Assign,
  CompoundAssign,
  Call,
  Index,
  Conditional,
  Cast,
  SizeOfType
};

struct Expression;
using ExpressionPtr = std::unique_ptr<Expression>;

struct Expression {
  ExpressionKind kind = ExpressionKind::IntLiteral;
  SourceLocation location;
  uint64_t literal = 0;
  bool literalUnsigned = false;
  bool literalLong = false;
  std::string name;
  UnaryOp unaryOp = UnaryOp::Plus;
  BinaryOp binaryOp = BinaryOp::Add;
  TypeRef typeOperand = nullptr;
  ExpressionPtr a;
  ExpressionPtr b;
  ExpressionPtr c;
  std::vector<ExpressionPtr> arguments;
};

enum class StatementKind {
  Compound,
  Declaration,
  ExpressionStatement,
  If,
  While,
  DoWhile,
  For,
  Return,
  Break,
  Continue,
  Empty
};

struct Statement;
using StatementPtr = std::unique_ptr<Statement>;

struct Statement {
  StatementKind kind = StatementKind::Empty;
  SourceLocation location;
  std::vector<StatementPtr> body;
  ExpressionPtr expression;
  ExpressionPtr condition;
  ExpressionPtr step;
  StatementPtr thenBranch;
  StatementPtr elseBranch;
  StatementPtr loopBody;
  StatementPtr initializerStatement;
  std::string declarationName;
  TypeRef declarationType = nullptr;
  ExpressionPtr initializer;
  std::vector<ExpressionPtr> initializerList;
};

struct Parameter {
  std::string name;
  TypeRef type = nullptr;
};

struct FunctionDeclaration {
  std::string name;
  TypeRef returnType = nullptr;
  std::vector<Parameter> parameters;
  StatementPtr body;
  SourceLocation location;
};

struct GlobalDeclaration {
  std::string name;
  TypeRef type = nullptr;
  std::vector<uint64_t> initialValues;
  SourceLocation location;
};

struct Program {
  std::vector<FunctionDeclaration> functions;
  std::vector<GlobalDeclaration> globals;
  std::string fileName;
};

}

#endif
