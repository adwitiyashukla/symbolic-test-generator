#include "ir/parser.h"

#include <deque>
#include <sstream>
#include <unordered_map>
#include <utility>

#include "util/support.h"

namespace symx {
namespace {

struct TypePool {
  std::deque<Type> nodes;
  std::unordered_map<std::string, TypeRef> table;
};

TypePool &typePool() {
  static TypePool pool;
  return pool;
}

TypeRef internType(const Type &type, const std::string &key) {
  TypePool &pool = typePool();
  auto found = pool.table.find(key);
  if (found != pool.table.end()) {
    return found->second;
  }
  pool.nodes.push_back(type);
  TypeRef ref = &pool.nodes.back();
  pool.table.emplace(key, ref);
  return ref;
}

int precedenceOf(TokenKind kind) {
  switch (kind) {
    case TokenKind::PipePipe:
      return 1;
    case TokenKind::AmpersandAmpersand:
      return 2;
    case TokenKind::Pipe:
      return 3;
    case TokenKind::Caret:
      return 4;
    case TokenKind::Ampersand:
      return 5;
    case TokenKind::EqualEqual:
    case TokenKind::BangEqual:
      return 6;
    case TokenKind::Less:
    case TokenKind::Greater:
    case TokenKind::LessEqual:
    case TokenKind::GreaterEqual:
      return 7;
    case TokenKind::LessLess:
    case TokenKind::GreaterGreater:
      return 8;
    case TokenKind::Plus:
    case TokenKind::Minus:
      return 9;
    case TokenKind::Star:
    case TokenKind::Slash:
    case TokenKind::Percent:
      return 10;
    default:
      return 0;
  }
}

BinaryOp binaryOpFor(TokenKind kind) {
  switch (kind) {
    case TokenKind::PipePipe:
      return BinaryOp::LogicalOr;
    case TokenKind::AmpersandAmpersand:
      return BinaryOp::LogicalAnd;
    case TokenKind::Pipe:
      return BinaryOp::BitOr;
    case TokenKind::Caret:
      return BinaryOp::BitXor;
    case TokenKind::Ampersand:
      return BinaryOp::BitAnd;
    case TokenKind::EqualEqual:
      return BinaryOp::Equal;
    case TokenKind::BangEqual:
      return BinaryOp::NotEqual;
    case TokenKind::Less:
      return BinaryOp::Less;
    case TokenKind::Greater:
      return BinaryOp::Greater;
    case TokenKind::LessEqual:
      return BinaryOp::LessEqual;
    case TokenKind::GreaterEqual:
      return BinaryOp::GreaterEqual;
    case TokenKind::LessLess:
      return BinaryOp::ShiftLeft;
    case TokenKind::GreaterGreater:
      return BinaryOp::ShiftRight;
    case TokenKind::Plus:
      return BinaryOp::Add;
    case TokenKind::Minus:
      return BinaryOp::Subtract;
    case TokenKind::Star:
      return BinaryOp::Multiply;
    case TokenKind::Slash:
      return BinaryOp::Divide;
    default:
      return BinaryOp::Modulo;
  }
}

bool compoundAssignOp(TokenKind kind, BinaryOp &op) {
  switch (kind) {
    case TokenKind::PlusAssign:
      op = BinaryOp::Add;
      return true;
    case TokenKind::MinusAssign:
      op = BinaryOp::Subtract;
      return true;
    case TokenKind::StarAssign:
      op = BinaryOp::Multiply;
      return true;
    case TokenKind::SlashAssign:
      op = BinaryOp::Divide;
      return true;
    case TokenKind::PercentAssign:
      op = BinaryOp::Modulo;
      return true;
    case TokenKind::AmpersandAssign:
      op = BinaryOp::BitAnd;
      return true;
    case TokenKind::PipeAssign:
      op = BinaryOp::BitOr;
      return true;
    case TokenKind::CaretAssign:
      op = BinaryOp::BitXor;
      return true;
    case TokenKind::LessLessAssign:
      op = BinaryOp::ShiftLeft;
      return true;
    case TokenKind::GreaterGreaterAssign:
      op = BinaryOp::ShiftRight;
      return true;
    default:
      return false;
  }
}

}

TypeRef voidType() {
  Type type;
  type.kind = TypeKind::Void;
  return internType(type, "void");
}

TypeRef integerType(unsigned bits, bool isSigned) {
  Type type;
  type.kind = TypeKind::Integer;
  type.bits = bits;
  type.isSigned = isSigned;
  std::ostringstream key;
  key << (isSigned ? "i" : "u") << bits;
  return internType(type, key.str());
}

TypeRef boolType() { return integerType(1, false); }

TypeRef pointerTo(TypeRef element) {
  Type type;
  type.kind = TypeKind::Pointer;
  type.bits = 64;
  type.element = element;
  std::ostringstream key;
  key << "p:" << reinterpret_cast<uintptr_t>(element);
  return internType(type, key.str());
}

TypeRef arrayOf(TypeRef element, uint64_t count) {
  Type type;
  type.kind = TypeKind::Array;
  type.element = element;
  type.count = count;
  std::ostringstream key;
  key << "a:" << reinterpret_cast<uintptr_t>(element) << ":" << count;
  return internType(type, key.str());
}

uint64_t typeSizeInBytes(TypeRef type) {
  switch (type->kind) {
    case TypeKind::Void:
      return 1;
    case TypeKind::Integer:
      return type->bits <= 8 ? 1 : type->bits / 8;
    case TypeKind::Pointer:
      return 8;
    case TypeKind::Array:
      return type->count * typeSizeInBytes(type->element);
  }
  return 0;
}

unsigned typeBitWidth(TypeRef type) {
  if (type->kind == TypeKind::Pointer) {
    return 64;
  }
  if (type->kind == TypeKind::Integer) {
    return type->bits;
  }
  return 64;
}

bool isIntegerType(TypeRef type) { return type->kind == TypeKind::Integer; }

bool isPointerType(TypeRef type) { return type->kind == TypeKind::Pointer; }

bool isArrayType(TypeRef type) { return type->kind == TypeKind::Array; }

bool isScalarType(TypeRef type) { return isIntegerType(type) || isPointerType(type); }

TypeRef decayType(TypeRef type) {
  if (isArrayType(type)) {
    return pointerTo(type->element);
  }
  return type;
}

std::string typeName(TypeRef type) {
  switch (type->kind) {
    case TypeKind::Void:
      return "void";
    case TypeKind::Integer: {
      std::ostringstream os;
      if (type->bits == 8) {
        os << (type->isSigned ? "char" : "unsigned char");
      } else if (type->bits == 16) {
        os << (type->isSigned ? "short" : "unsigned short");
      } else if (type->bits == 32) {
        os << (type->isSigned ? "int" : "unsigned int");
      } else {
        os << (type->isSigned ? "long" : "unsigned long");
      }
      return os.str();
    }
    case TypeKind::Pointer:
      return typeName(type->element) + " *";
    case TypeKind::Array: {
      std::ostringstream os;
      os << typeName(type->element) << "[" << type->count << "]";
      return os.str();
    }
  }
  return "?";
}

Parser::Parser(std::vector<Token> tokens, std::string fileName)
    : tokens_(std::move(tokens)), fileName_(std::move(fileName)) {}

const Token &Parser::current() const { return tokens_[position_]; }

const Token &Parser::lookahead(size_t distance) const {
  size_t index = position_ + distance;
  return index < tokens_.size() ? tokens_[index] : tokens_.back();
}

bool Parser::accept(TokenKind kind) {
  if (current().kind == kind) {
    position_++;
    return true;
  }
  return false;
}

void Parser::failAt(const SourceLocation &location, const std::string &message) {
  if (failed_) {
    return;
  }
  failed_ = true;
  std::ostringstream os;
  os << fileName_ << ":" << location.line << ":" << location.column << ": " << message;
  error_ = os.str();
}

void Parser::fail(const std::string &message) { failAt(current().location, message); }

bool Parser::expect(TokenKind kind, const char *what) {
  if (accept(kind)) {
    return true;
  }
  fail(std::string("expected ") + what);
  return false;
}

bool Parser::startsType(size_t offset) const {
  switch (lookahead(offset).kind) {
    case TokenKind::KeywordInt:
    case TokenKind::KeywordChar:
    case TokenKind::KeywordShort:
    case TokenKind::KeywordLong:
    case TokenKind::KeywordUnsigned:
    case TokenKind::KeywordSigned:
    case TokenKind::KeywordVoid:
    case TokenKind::KeywordConst:
    case TokenKind::KeywordStatic:
      return true;
    default:
      return false;
  }
}

TypeRef Parser::parseTypeSpecifier() {
  bool sawUnsigned = false;
  bool sawSigned = false;
  bool sawVoid = false;
  bool sawChar = false;
  bool sawShort = false;
  bool sawInt = false;
  int longCount = 0;
  bool sawAny = false;
  for (;;) {
    if (accept(TokenKind::KeywordConst) || accept(TokenKind::KeywordStatic)) {
      continue;
    }
    if (accept(TokenKind::KeywordUnsigned)) {
      sawUnsigned = true;
      sawAny = true;
      continue;
    }
    if (accept(TokenKind::KeywordSigned)) {
      sawSigned = true;
      sawAny = true;
      continue;
    }
    if (accept(TokenKind::KeywordVoid)) {
      sawVoid = true;
      sawAny = true;
      continue;
    }
    if (accept(TokenKind::KeywordChar)) {
      sawChar = true;
      sawAny = true;
      continue;
    }
    if (accept(TokenKind::KeywordShort)) {
      sawShort = true;
      sawAny = true;
      continue;
    }
    if (accept(TokenKind::KeywordInt)) {
      sawInt = true;
      sawAny = true;
      continue;
    }
    if (accept(TokenKind::KeywordLong)) {
      longCount++;
      sawAny = true;
      continue;
    }
    break;
  }
  if (!sawAny) {
    fail("expected a type");
    return integerType(32, true);
  }
  if (sawVoid) {
    return voidType();
  }
  bool isSigned = !sawUnsigned;
  if (sawSigned) {
    isSigned = true;
  }
  if (sawChar) {
    return integerType(8, isSigned);
  }
  if (sawShort) {
    return integerType(16, isSigned);
  }
  if (longCount > 0) {
    return integerType(64, isSigned);
  }
  if (sawInt || sawUnsigned || sawSigned) {
    return integerType(32, isSigned);
  }
  return integerType(32, true);
}

bool Parser::parseDeclarator(TypeRef base, std::string &name, TypeRef &result) {
  TypeRef type = base;
  while (accept(TokenKind::Star)) {
    while (accept(TokenKind::KeywordConst)) {
    }
    type = pointerTo(type);
  }
  if (current().kind != TokenKind::Identifier) {
    fail("expected an identifier in declaration");
    return false;
  }
  name = current().text;
  position_++;
  std::vector<uint64_t> dimensions;
  while (accept(TokenKind::LeftBracket)) {
    if (current().kind != TokenKind::Number) {
      fail("array bound must be an integer literal");
      return false;
    }
    dimensions.push_back(current().value);
    position_++;
    if (!expect(TokenKind::RightBracket, "]")) {
      return false;
    }
  }
  for (size_t i = dimensions.size(); i > 0; i--) {
    type = arrayOf(type, dimensions[i - 1]);
  }
  result = type;
  return true;
}

TypeRef Parser::parseTypeName() {
  TypeRef base = parseTypeSpecifier();
  while (accept(TokenKind::Star)) {
    base = pointerTo(base);
  }
  return base;
}

ExpressionPtr Parser::makeExpression(ExpressionKind kind, const SourceLocation &location) {
  ExpressionPtr node(new Expression());
  node->kind = kind;
  node->location = location;
  return node;
}

ExpressionPtr Parser::parsePrimary() {
  SourceLocation location = current().location;
  if (current().kind == TokenKind::Number) {
    ExpressionPtr node = makeExpression(ExpressionKind::IntLiteral, location);
    node->literal = current().value;
    node->literalUnsigned = current().isUnsignedLiteral;
    node->literalLong = current().isLongLiteral;
    position_++;
    return node;
  }
  if (current().kind == TokenKind::Identifier) {
    ExpressionPtr node = makeExpression(ExpressionKind::Identifier, location);
    node->name = current().text;
    position_++;
    return node;
  }
  if (accept(TokenKind::LeftParen)) {
    ExpressionPtr inner = parseExpression();
    expect(TokenKind::RightParen, ")");
    return inner;
  }
  fail("expected an expression");
  return makeExpression(ExpressionKind::IntLiteral, location);
}

ExpressionPtr Parser::parsePostfix() {
  ExpressionPtr node = parsePrimary();
  for (;;) {
    SourceLocation location = current().location;
    if (accept(TokenKind::LeftBracket)) {
      ExpressionPtr index = makeExpression(ExpressionKind::Index, location);
      index->a = std::move(node);
      index->b = parseExpression();
      expect(TokenKind::RightBracket, "]");
      node = std::move(index);
      continue;
    }
    if (current().kind == TokenKind::LeftParen && node->kind == ExpressionKind::Identifier) {
      position_++;
      ExpressionPtr call = makeExpression(ExpressionKind::Call, location);
      call->name = node->name;
      if (!accept(TokenKind::RightParen)) {
        for (;;) {
          call->arguments.push_back(parseAssignment());
          if (accept(TokenKind::Comma)) {
            continue;
          }
          expect(TokenKind::RightParen, ")");
          break;
        }
      }
      node = std::move(call);
      continue;
    }
    if (accept(TokenKind::PlusPlus)) {
      ExpressionPtr unary = makeExpression(ExpressionKind::Unary, location);
      unary->unaryOp = UnaryOp::PostIncrement;
      unary->a = std::move(node);
      node = std::move(unary);
      continue;
    }
    if (accept(TokenKind::MinusMinus)) {
      ExpressionPtr unary = makeExpression(ExpressionKind::Unary, location);
      unary->unaryOp = UnaryOp::PostDecrement;
      unary->a = std::move(node);
      node = std::move(unary);
      continue;
    }
    return node;
  }
}

ExpressionPtr Parser::parseUnary() {
  SourceLocation location = current().location;
  if (current().kind == TokenKind::LeftParen && startsType(1)) {
    position_++;
    TypeRef target = parseTypeName();
    expect(TokenKind::RightParen, ")");
    ExpressionPtr node = makeExpression(ExpressionKind::Cast, location);
    node->typeOperand = target;
    node->a = parseUnary();
    return node;
  }
  if (current().kind == TokenKind::KeywordSizeof) {
    position_++;
    ExpressionPtr node = makeExpression(ExpressionKind::SizeOfType, location);
    if (current().kind == TokenKind::LeftParen && startsType(1)) {
      position_++;
      node->typeOperand = parseTypeName();
      expect(TokenKind::RightParen, ")");
      return node;
    }
    node->a = parseUnary();
    return node;
  }
  UnaryOp op = UnaryOp::Plus;
  bool matched = true;
  if (accept(TokenKind::Minus)) {
    op = UnaryOp::Minus;
  } else if (accept(TokenKind::Plus)) {
    op = UnaryOp::Plus;
  } else if (accept(TokenKind::Bang)) {
    op = UnaryOp::LogicalNot;
  } else if (accept(TokenKind::Tilde)) {
    op = UnaryOp::BitNot;
  } else if (accept(TokenKind::Star)) {
    op = UnaryOp::Dereference;
  } else if (accept(TokenKind::Ampersand)) {
    op = UnaryOp::AddressOf;
  } else if (accept(TokenKind::PlusPlus)) {
    op = UnaryOp::PreIncrement;
  } else if (accept(TokenKind::MinusMinus)) {
    op = UnaryOp::PreDecrement;
  } else {
    matched = false;
  }
  if (!matched) {
    return parsePostfix();
  }
  ExpressionPtr node = makeExpression(ExpressionKind::Unary, location);
  node->unaryOp = op;
  node->a = parseUnary();
  return node;
}

ExpressionPtr Parser::parseBinary(int minimumPrecedence) {
  ExpressionPtr left = parseUnary();
  for (;;) {
    int precedence = precedenceOf(current().kind);
    if (precedence == 0 || precedence < minimumPrecedence) {
      return left;
    }
    TokenKind kind = current().kind;
    SourceLocation location = current().location;
    position_++;
    ExpressionPtr right = parseBinary(precedence + 1);
    ExpressionPtr node = makeExpression(ExpressionKind::Binary, location);
    node->binaryOp = binaryOpFor(kind);
    node->a = std::move(left);
    node->b = std::move(right);
    left = std::move(node);
  }
}

ExpressionPtr Parser::parseConditional() {
  ExpressionPtr condition = parseBinary(1);
  if (!accept(TokenKind::Question)) {
    return condition;
  }
  SourceLocation location = current().location;
  ExpressionPtr node = makeExpression(ExpressionKind::Conditional, location);
  node->a = std::move(condition);
  node->b = parseAssignment();
  expect(TokenKind::Colon, ":");
  node->c = parseConditional();
  return node;
}

ExpressionPtr Parser::parseAssignment() {
  ExpressionPtr left = parseConditional();
  SourceLocation location = current().location;
  if (accept(TokenKind::Assign)) {
    ExpressionPtr node = makeExpression(ExpressionKind::Assign, location);
    node->a = std::move(left);
    node->b = parseAssignment();
    return node;
  }
  BinaryOp op = BinaryOp::Add;
  if (compoundAssignOp(current().kind, op)) {
    position_++;
    ExpressionPtr node = makeExpression(ExpressionKind::CompoundAssign, location);
    node->binaryOp = op;
    node->a = std::move(left);
    node->b = parseAssignment();
    return node;
  }
  return left;
}

ExpressionPtr Parser::parseExpression() {
  ExpressionPtr left = parseAssignment();
  while (current().kind == TokenKind::Comma) {
    SourceLocation location = current().location;
    position_++;
    ExpressionPtr node = makeExpression(ExpressionKind::Binary, location);
    node->binaryOp = BinaryOp::Comma;
    node->a = std::move(left);
    node->b = parseAssignment();
    left = std::move(node);
  }
  return left;
}

StatementPtr Parser::parseDeclaration() {
  SourceLocation location = current().location;
  TypeRef base = parseTypeSpecifier();
  StatementPtr first;
  StatementPtr group(new Statement());
  group->kind = StatementKind::Compound;
  group->location = location;
  for (;;) {
    std::string name;
    TypeRef type = base;
    if (!parseDeclarator(base, name, type)) {
      return group;
    }
    StatementPtr declaration(new Statement());
    declaration->kind = StatementKind::Declaration;
    declaration->location = location;
    declaration->declarationName = name;
    declaration->declarationType = type;
    if (accept(TokenKind::Assign)) {
      if (accept(TokenKind::LeftBrace)) {
        if (!accept(TokenKind::RightBrace)) {
          for (;;) {
            declaration->initializerList.push_back(parseAssignment());
            if (accept(TokenKind::Comma)) {
              if (current().kind == TokenKind::RightBrace) {
                position_++;
                break;
              }
              continue;
            }
            expect(TokenKind::RightBrace, "}");
            break;
          }
        }
      } else {
        declaration->initializer = parseAssignment();
      }
    }
    group->body.push_back(std::move(declaration));
    if (accept(TokenKind::Comma)) {
      continue;
    }
    expect(TokenKind::Semicolon, ";");
    break;
  }
  if (group->body.size() == 1) {
    first = std::move(group->body[0]);
    return first;
  }
  return group;
}

StatementPtr Parser::parseCompound() {
  SourceLocation location = current().location;
  StatementPtr node(new Statement());
  node->kind = StatementKind::Compound;
  node->location = location;
  expect(TokenKind::LeftBrace, "{");
  while (!failed_ && current().kind != TokenKind::RightBrace && current().kind != TokenKind::End) {
    node->body.push_back(parseStatement());
  }
  expect(TokenKind::RightBrace, "}");
  return node;
}

StatementPtr Parser::parseStatement() {
  SourceLocation location = current().location;
  if (failed_) {
    StatementPtr node(new Statement());
    node->kind = StatementKind::Empty;
    return node;
  }
  if (current().kind == TokenKind::LeftBrace) {
    return parseCompound();
  }
  if (startsType(0)) {
    return parseDeclaration();
  }
  if (accept(TokenKind::Semicolon)) {
    StatementPtr node(new Statement());
    node->kind = StatementKind::Empty;
    node->location = location;
    return node;
  }
  if (accept(TokenKind::KeywordIf)) {
    StatementPtr node(new Statement());
    node->kind = StatementKind::If;
    node->location = location;
    expect(TokenKind::LeftParen, "(");
    node->condition = parseExpression();
    expect(TokenKind::RightParen, ")");
    node->thenBranch = parseStatement();
    if (accept(TokenKind::KeywordElse)) {
      node->elseBranch = parseStatement();
    }
    return node;
  }
  if (accept(TokenKind::KeywordWhile)) {
    StatementPtr node(new Statement());
    node->kind = StatementKind::While;
    node->location = location;
    expect(TokenKind::LeftParen, "(");
    node->condition = parseExpression();
    expect(TokenKind::RightParen, ")");
    node->loopBody = parseStatement();
    return node;
  }
  if (accept(TokenKind::KeywordDo)) {
    StatementPtr node(new Statement());
    node->kind = StatementKind::DoWhile;
    node->location = location;
    node->loopBody = parseStatement();
    expect(TokenKind::KeywordWhile, "while");
    expect(TokenKind::LeftParen, "(");
    node->condition = parseExpression();
    expect(TokenKind::RightParen, ")");
    expect(TokenKind::Semicolon, ";");
    return node;
  }
  if (accept(TokenKind::KeywordFor)) {
    StatementPtr node(new Statement());
    node->kind = StatementKind::For;
    node->location = location;
    expect(TokenKind::LeftParen, "(");
    if (current().kind == TokenKind::Semicolon) {
      position_++;
    } else if (startsType(0)) {
      node->initializerStatement = parseDeclaration();
    } else {
      StatementPtr initializer(new Statement());
      initializer->kind = StatementKind::ExpressionStatement;
      initializer->location = current().location;
      initializer->expression = parseExpression();
      expect(TokenKind::Semicolon, ";");
      node->initializerStatement = std::move(initializer);
    }
    if (current().kind != TokenKind::Semicolon) {
      node->condition = parseExpression();
    }
    expect(TokenKind::Semicolon, ";");
    if (current().kind != TokenKind::RightParen) {
      node->step = parseExpression();
    }
    expect(TokenKind::RightParen, ")");
    node->loopBody = parseStatement();
    return node;
  }
  if (accept(TokenKind::KeywordReturn)) {
    StatementPtr node(new Statement());
    node->kind = StatementKind::Return;
    node->location = location;
    if (current().kind != TokenKind::Semicolon) {
      node->expression = parseExpression();
    }
    expect(TokenKind::Semicolon, ";");
    return node;
  }
  if (accept(TokenKind::KeywordBreak)) {
    StatementPtr node(new Statement());
    node->kind = StatementKind::Break;
    node->location = location;
    expect(TokenKind::Semicolon, ";");
    return node;
  }
  if (accept(TokenKind::KeywordContinue)) {
    StatementPtr node(new Statement());
    node->kind = StatementKind::Continue;
    node->location = location;
    expect(TokenKind::Semicolon, ";");
    return node;
  }
  StatementPtr node(new Statement());
  node->kind = StatementKind::ExpressionStatement;
  node->location = location;
  node->expression = parseExpression();
  expect(TokenKind::Semicolon, ";");
  return node;
}

bool Parser::parseProgram(Program &program, std::string &error) {
  program.fileName = fileName_;
  while (!failed_ && current().kind != TokenKind::End) {
    SourceLocation location = current().location;
    TypeRef base = parseTypeSpecifier();
    if (failed_) {
      break;
    }
    std::string name;
    TypeRef declaredType = base;
    TypeRef pointerBase = base;
    while (accept(TokenKind::Star)) {
      pointerBase = pointerTo(pointerBase);
    }
    if (current().kind != TokenKind::Identifier) {
      fail("expected a declaration name");
      break;
    }
    name = current().text;
    position_++;
    if (current().kind == TokenKind::LeftParen) {
      position_++;
      FunctionDeclaration function;
      function.name = name;
      function.returnType = pointerBase;
      function.location = location;
      if (!accept(TokenKind::RightParen)) {
        if (current().kind == TokenKind::KeywordVoid &&
            lookahead(1).kind == TokenKind::RightParen) {
          position_ += 2;
        } else {
          for (;;) {
            TypeRef parameterBase = parseTypeSpecifier();
            std::string parameterName;
            TypeRef parameterType = parameterBase;
            if (!parseDeclarator(parameterBase, parameterName, parameterType)) {
              break;
            }
            Parameter parameter;
            parameter.name = parameterName;
            parameter.type = decayType(parameterType);
            function.parameters.push_back(parameter);
            if (accept(TokenKind::Comma)) {
              continue;
            }
            expect(TokenKind::RightParen, ")");
            break;
          }
        }
      }
      if (accept(TokenKind::Semicolon)) {
        continue;
      }
      function.body = parseCompound();
      program.functions.push_back(std::move(function));
      continue;
    }
    declaredType = pointerBase;
    std::vector<uint64_t> dimensions;
    while (accept(TokenKind::LeftBracket)) {
      if (current().kind != TokenKind::Number) {
        fail("array bound must be an integer literal");
        break;
      }
      dimensions.push_back(current().value);
      position_++;
      expect(TokenKind::RightBracket, "]");
    }
    for (size_t i = dimensions.size(); i > 0; i--) {
      declaredType = arrayOf(declaredType, dimensions[i - 1]);
    }
    GlobalDeclaration global;
    global.name = name;
    global.type = declaredType;
    global.location = location;
    if (accept(TokenKind::Assign)) {
      if (accept(TokenKind::LeftBrace)) {
        while (!accept(TokenKind::RightBrace) && !failed_) {
          bool negative = accept(TokenKind::Minus);
          if (current().kind != TokenKind::Number) {
            fail("global initializers must be integer literals");
            break;
          }
          uint64_t value = current().value;
          position_++;
          global.initialValues.push_back(negative ? static_cast<uint64_t>(0) - value : value);
          if (!accept(TokenKind::Comma)) {
            expect(TokenKind::RightBrace, "}");
            break;
          }
        }
      } else {
        bool negative = accept(TokenKind::Minus);
        if (current().kind != TokenKind::Number) {
          fail("global initializers must be integer literals");
          break;
        }
        uint64_t value = current().value;
        position_++;
        global.initialValues.push_back(negative ? static_cast<uint64_t>(0) - value : value);
      }
    }
    expect(TokenKind::Semicolon, ";");
    program.globals.push_back(global);
  }
  if (failed_) {
    error = error_;
    return false;
  }
  return true;
}

}
