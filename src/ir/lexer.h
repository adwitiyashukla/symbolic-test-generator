#ifndef SYMX_IR_LEXER_H
#define SYMX_IR_LEXER_H

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace symx {

enum class TokenKind {
  End,
  Identifier,
  Number,
  KeywordInt,
  KeywordChar,
  KeywordShort,
  KeywordLong,
  KeywordUnsigned,
  KeywordSigned,
  KeywordVoid,
  KeywordIf,
  KeywordElse,
  KeywordWhile,
  KeywordFor,
  KeywordDo,
  KeywordReturn,
  KeywordBreak,
  KeywordContinue,
  KeywordSizeof,
  KeywordConst,
  KeywordStatic,
  LeftParen,
  RightParen,
  LeftBrace,
  RightBrace,
  LeftBracket,
  RightBracket,
  Semicolon,
  Comma,
  Question,
  Colon,
  Assign,
  Plus,
  Minus,
  Star,
  Slash,
  Percent,
  Ampersand,
  Pipe,
  Caret,
  Tilde,
  Bang,
  Less,
  Greater,
  LessEqual,
  GreaterEqual,
  EqualEqual,
  BangEqual,
  AmpersandAmpersand,
  PipePipe,
  LessLess,
  GreaterGreater,
  PlusPlus,
  MinusMinus,
  PlusAssign,
  MinusAssign,
  StarAssign,
  SlashAssign,
  PercentAssign,
  AmpersandAssign,
  PipeAssign,
  CaretAssign,
  LessLessAssign,
  GreaterGreaterAssign
};

struct SourceLocation {
  int line = 0;
  int column = 0;
};

struct Token {
  TokenKind kind = TokenKind::End;
  std::string text;
  uint64_t value = 0;
  bool isUnsignedLiteral = false;
  bool isLongLiteral = false;
  SourceLocation location;
};

const char *tokenKindName(TokenKind kind);

class Lexer {
 public:
  Lexer(const std::string &source, const std::string &fileName);

  bool tokenize(std::string &error);
  const std::vector<Token> &tokens() const { return tokens_; }

 private:
  void skipTrivia();
  bool lexDirective(std::string &error);
  bool lexNumber(std::string &error);
  bool lexCharacter(std::string &error);
  void lexIdentifier();
  bool lexPunctuation(std::string &error);
  char peek(size_t ahead = 0) const;
  void advance();
  SourceLocation here() const;

  std::string source_;
  std::string fileName_;
  size_t position_ = 0;
  int line_ = 1;
  int column_ = 1;
  std::vector<Token> tokens_;
  std::unordered_map<std::string, Token> macros_;
};

}

#endif
