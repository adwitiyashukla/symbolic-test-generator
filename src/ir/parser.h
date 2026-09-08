#ifndef SYMX_IR_PARSER_H
#define SYMX_IR_PARSER_H

#include <cstdint>
#include <string>
#include <vector>

#include "ir/ast.h"
#include "ir/lexer.h"

namespace symx {

class Parser {
 public:
  Parser(std::vector<Token> tokens, std::string fileName);

  bool parseProgram(Program &program, std::string &error);

 private:
  const Token &current() const;
  const Token &lookahead(size_t distance) const;
  bool accept(TokenKind kind);
  bool expect(TokenKind kind, const char *what);
  void fail(const std::string &message);
  void failAt(const SourceLocation &location, const std::string &message);
  bool startsType(size_t offset) const;

  TypeRef parseTypeSpecifier();
  bool parseDeclarator(TypeRef base, std::string &name, TypeRef &result);
  TypeRef parseTypeName();

  StatementPtr parseStatement();
  StatementPtr parseCompound();
  StatementPtr parseDeclaration();

  ExpressionPtr parseExpression();
  ExpressionPtr parseAssignment();
  ExpressionPtr parseConditional();
  ExpressionPtr parseBinary(int minimumPrecedence);
  ExpressionPtr parseUnary();
  ExpressionPtr parsePostfix();
  ExpressionPtr parsePrimary();

  ExpressionPtr makeExpression(ExpressionKind kind, const SourceLocation &location);

  std::vector<Token> tokens_;
  std::string fileName_;
  size_t position_ = 0;
  bool failed_ = false;
  std::string error_;
};

}

#endif
