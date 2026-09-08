#include "ir/lexer.h"

#include <cctype>
#include <cstdlib>
#include <sstream>

namespace symx {
namespace {

const std::unordered_map<std::string, TokenKind> &keywordTable() {
  static const std::unordered_map<std::string, TokenKind> table = {
      {"int", TokenKind::KeywordInt},           {"char", TokenKind::KeywordChar},
      {"short", TokenKind::KeywordShort},       {"long", TokenKind::KeywordLong},
      {"unsigned", TokenKind::KeywordUnsigned}, {"signed", TokenKind::KeywordSigned},
      {"void", TokenKind::KeywordVoid},         {"if", TokenKind::KeywordIf},
      {"else", TokenKind::KeywordElse},         {"while", TokenKind::KeywordWhile},
      {"for", TokenKind::KeywordFor},           {"do", TokenKind::KeywordDo},
      {"return", TokenKind::KeywordReturn},     {"break", TokenKind::KeywordBreak},
      {"continue", TokenKind::KeywordContinue}, {"sizeof", TokenKind::KeywordSizeof},
      {"const", TokenKind::KeywordConst},       {"static", TokenKind::KeywordStatic}};
  return table;
}

}

const char *tokenKindName(TokenKind kind) {
  switch (kind) {
    case TokenKind::End:
      return "end of file";
    case TokenKind::Identifier:
      return "identifier";
    case TokenKind::Number:
      return "number";
    case TokenKind::LeftParen:
      return "(";
    case TokenKind::RightParen:
      return ")";
    case TokenKind::LeftBrace:
      return "{";
    case TokenKind::RightBrace:
      return "}";
    case TokenKind::LeftBracket:
      return "[";
    case TokenKind::RightBracket:
      return "]";
    case TokenKind::Semicolon:
      return ";";
    case TokenKind::Comma:
      return ",";
    case TokenKind::Assign:
      return "=";
    default:
      return "token";
  }
}

Lexer::Lexer(const std::string &source, const std::string &fileName)
    : source_(source), fileName_(fileName) {}

char Lexer::peek(size_t ahead) const {
  size_t index = position_ + ahead;
  return index < source_.size() ? source_[index] : '\0';
}

void Lexer::advance() {
  if (position_ >= source_.size()) {
    return;
  }
  if (source_[position_] == '\n') {
    line_++;
    column_ = 1;
  } else {
    column_++;
  }
  position_++;
}

SourceLocation Lexer::here() const {
  SourceLocation location;
  location.line = line_;
  location.column = column_;
  return location;
}

void Lexer::skipTrivia() {
  for (;;) {
    char c = peek();
    if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
      advance();
      continue;
    }
    if (c == '/' && peek(1) == '/') {
      while (peek() != '\n' && peek() != '\0') {
        advance();
      }
      continue;
    }
    if (c == '/' && peek(1) == '*') {
      advance();
      advance();
      while (peek() != '\0' && !(peek() == '*' && peek(1) == '/')) {
        advance();
      }
      advance();
      advance();
      continue;
    }
    return;
  }
}

bool Lexer::lexDirective(std::string &error) {
  SourceLocation location = here();
  advance();
  std::string directive;
  while (std::isalpha(static_cast<unsigned char>(peek()))) {
    directive.push_back(peek());
    advance();
  }
  if (directive == "define") {
    while (peek() == ' ' || peek() == '\t') {
      advance();
    }
    std::string name;
    while (std::isalnum(static_cast<unsigned char>(peek())) || peek() == '_') {
      name.push_back(peek());
      advance();
    }
    while (peek() == ' ' || peek() == '\t') {
      advance();
    }
    std::string body;
    while (peek() != '\n' && peek() != '\0') {
      body.push_back(peek());
      advance();
    }
    if (name.empty()) {
      error = "malformed define directive";
      return false;
    }
    Token token;
    token.kind = TokenKind::Number;
    token.location = location;
    std::string trimmedBody;
    for (char c : body) {
      if (c != ' ' && c != '\t' && c != '\r') {
        trimmedBody.push_back(c);
      }
    }
    if (trimmedBody.empty()) {
      macros_.erase(name);
      return true;
    }
    bool negative = false;
    size_t start = 0;
    if (trimmedBody[0] == '-') {
      negative = true;
      start = 1;
    }
    char *end = nullptr;
    unsigned long long parsed = std::strtoull(trimmedBody.c_str() + start, &end, 0);
    if (end == trimmedBody.c_str() + start) {
      macros_.erase(name);
      return true;
    }
    token.value = negative ? static_cast<uint64_t>(-static_cast<int64_t>(parsed)) : parsed;
    token.text = trimmedBody;
    macros_[name] = token;
    return true;
  }
  while (peek() != '\n' && peek() != '\0') {
    advance();
  }
  return true;
}

bool Lexer::lexNumber(std::string &error) {
  SourceLocation location = here();
  std::string text;
  while (std::isalnum(static_cast<unsigned char>(peek())) || peek() == '.') {
    text.push_back(peek());
    advance();
  }
  std::string digits = text;
  bool isUnsigned = false;
  bool isLong = false;
  while (!digits.empty()) {
    char last = digits.back();
    if (last == 'u' || last == 'U') {
      isUnsigned = true;
      digits.pop_back();
    } else if (last == 'l' || last == 'L') {
      isLong = true;
      digits.pop_back();
    } else {
      break;
    }
  }
  char *end = nullptr;
  unsigned long long value = std::strtoull(digits.c_str(), &end, 0);
  if (digits.empty() || *end != '\0') {
    std::ostringstream os;
    os << fileName_ << ":" << location.line << ": invalid number literal " << text;
    error = os.str();
    return false;
  }
  Token token;
  token.kind = TokenKind::Number;
  token.value = value;
  token.text = text;
  token.isUnsignedLiteral = isUnsigned;
  token.isLongLiteral = isLong;
  token.location = location;
  tokens_.push_back(token);
  return true;
}

bool Lexer::lexCharacter(std::string &error) {
  SourceLocation location = here();
  advance();
  uint64_t value = 0;
  if (peek() == '\\') {
    advance();
    char escape = peek();
    advance();
    switch (escape) {
      case 'n':
        value = '\n';
        break;
      case 't':
        value = '\t';
        break;
      case 'r':
        value = '\r';
        break;
      case '0':
        value = 0;
        break;
      case '\\':
        value = '\\';
        break;
      case '\'':
        value = '\'';
        break;
      default:
        value = static_cast<uint64_t>(escape);
        break;
    }
  } else {
    value = static_cast<uint64_t>(static_cast<unsigned char>(peek()));
    advance();
  }
  if (peek() != '\'') {
    std::ostringstream os;
    os << fileName_ << ":" << location.line << ": unterminated character literal";
    error = os.str();
    return false;
  }
  advance();
  Token token;
  token.kind = TokenKind::Number;
  token.value = value;
  token.text = "char";
  token.location = location;
  tokens_.push_back(token);
  return true;
}

void Lexer::lexIdentifier() {
  SourceLocation location = here();
  std::string text;
  while (std::isalnum(static_cast<unsigned char>(peek())) || peek() == '_') {
    text.push_back(peek());
    advance();
  }
  auto macro = macros_.find(text);
  if (macro != macros_.end()) {
    Token token = macro->second;
    token.location = location;
    tokens_.push_back(token);
    return;
  }
  Token token;
  token.text = text;
  token.location = location;
  auto keyword = keywordTable().find(text);
  token.kind = keyword == keywordTable().end() ? TokenKind::Identifier : keyword->second;
  tokens_.push_back(token);
}

bool Lexer::lexPunctuation(std::string &error) {
  SourceLocation location = here();
  char c = peek();
  char next = peek(1);
  char third = peek(2);
  TokenKind kind = TokenKind::End;
  int length = 1;
  if (c == '<' && next == '<' && third == '=') {
    kind = TokenKind::LessLessAssign;
    length = 3;
  } else if (c == '>' && next == '>' && third == '=') {
    kind = TokenKind::GreaterGreaterAssign;
    length = 3;
  } else if (c == '=' && next == '=') {
    kind = TokenKind::EqualEqual;
    length = 2;
  } else if (c == '!' && next == '=') {
    kind = TokenKind::BangEqual;
    length = 2;
  } else if (c == '<' && next == '=') {
    kind = TokenKind::LessEqual;
    length = 2;
  } else if (c == '>' && next == '=') {
    kind = TokenKind::GreaterEqual;
    length = 2;
  } else if (c == '&' && next == '&') {
    kind = TokenKind::AmpersandAmpersand;
    length = 2;
  } else if (c == '|' && next == '|') {
    kind = TokenKind::PipePipe;
    length = 2;
  } else if (c == '<' && next == '<') {
    kind = TokenKind::LessLess;
    length = 2;
  } else if (c == '>' && next == '>') {
    kind = TokenKind::GreaterGreater;
    length = 2;
  } else if (c == '+' && next == '+') {
    kind = TokenKind::PlusPlus;
    length = 2;
  } else if (c == '-' && next == '-') {
    kind = TokenKind::MinusMinus;
    length = 2;
  } else if (c == '+' && next == '=') {
    kind = TokenKind::PlusAssign;
    length = 2;
  } else if (c == '-' && next == '=') {
    kind = TokenKind::MinusAssign;
    length = 2;
  } else if (c == '*' && next == '=') {
    kind = TokenKind::StarAssign;
    length = 2;
  } else if (c == '/' && next == '=') {
    kind = TokenKind::SlashAssign;
    length = 2;
  } else if (c == '%' && next == '=') {
    kind = TokenKind::PercentAssign;
    length = 2;
  } else if (c == '&' && next == '=') {
    kind = TokenKind::AmpersandAssign;
    length = 2;
  } else if (c == '|' && next == '=') {
    kind = TokenKind::PipeAssign;
    length = 2;
  } else if (c == '^' && next == '=') {
    kind = TokenKind::CaretAssign;
    length = 2;
  } else {
    switch (c) {
      case '(':
        kind = TokenKind::LeftParen;
        break;
      case ')':
        kind = TokenKind::RightParen;
        break;
      case '{':
        kind = TokenKind::LeftBrace;
        break;
      case '}':
        kind = TokenKind::RightBrace;
        break;
      case '[':
        kind = TokenKind::LeftBracket;
        break;
      case ']':
        kind = TokenKind::RightBracket;
        break;
      case ';':
        kind = TokenKind::Semicolon;
        break;
      case ',':
        kind = TokenKind::Comma;
        break;
      case '?':
        kind = TokenKind::Question;
        break;
      case ':':
        kind = TokenKind::Colon;
        break;
      case '=':
        kind = TokenKind::Assign;
        break;
      case '+':
        kind = TokenKind::Plus;
        break;
      case '-':
        kind = TokenKind::Minus;
        break;
      case '*':
        kind = TokenKind::Star;
        break;
      case '/':
        kind = TokenKind::Slash;
        break;
      case '%':
        kind = TokenKind::Percent;
        break;
      case '&':
        kind = TokenKind::Ampersand;
        break;
      case '|':
        kind = TokenKind::Pipe;
        break;
      case '^':
        kind = TokenKind::Caret;
        break;
      case '~':
        kind = TokenKind::Tilde;
        break;
      case '!':
        kind = TokenKind::Bang;
        break;
      case '<':
        kind = TokenKind::Less;
        break;
      case '>':
        kind = TokenKind::Greater;
        break;
      default: {
        std::ostringstream os;
        os << fileName_ << ":" << location.line << ": unexpected character '" << c << "'";
        error = os.str();
        return false;
      }
    }
  }
  for (int i = 0; i < length; i++) {
    advance();
  }
  Token token;
  token.kind = kind;
  token.location = location;
  tokens_.push_back(token);
  return true;
}

bool Lexer::tokenize(std::string &error) {
  for (;;) {
    skipTrivia();
    char c = peek();
    if (c == '\0') {
      break;
    }
    if (c == '#') {
      if (!lexDirective(error)) {
        return false;
      }
      continue;
    }
    if (std::isdigit(static_cast<unsigned char>(c))) {
      if (!lexNumber(error)) {
        return false;
      }
      continue;
    }
    if (c == '\'') {
      if (!lexCharacter(error)) {
        return false;
      }
      continue;
    }
    if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
      lexIdentifier();
      continue;
    }
    if (!lexPunctuation(error)) {
      return false;
    }
  }
  Token endToken;
  endToken.kind = TokenKind::End;
  endToken.location = here();
  tokens_.push_back(endToken);
  return true;
}

}
