#ifndef SYMX_SOLVER_EXPR_H
#define SYMX_SOLVER_EXPR_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace symx {

enum class ExprKind : uint8_t {
  Constant,
  Symbol,
  Concat,
  Extract,
  ZExt,
  SExt,
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
  Not,
  Eq,
  Ult,
  Ule,
  Slt,
  Sle,
  Ite
};

struct Expr;
using ExprRef = const Expr *;

struct Expr {
  ExprKind kind;
  uint16_t width;
  uint64_t value;
  ExprRef a;
  ExprRef b;
  ExprRef c;
  uint64_t hash;
};

const char *exprKindName(ExprKind kind);

uint64_t maskFor(unsigned width);
uint64_t truncateTo(uint64_t value, unsigned width);
uint64_t signExtendFrom(uint64_t value, unsigned width);
int64_t asSigned(uint64_t value, unsigned width);

bool isConstant(ExprRef e);
uint64_t constantValue(ExprRef e);
bool isTrue(ExprRef e);
bool isFalse(ExprRef e);

unsigned addSymbolByte(const std::string &name, unsigned index);
const std::string &symbolArrayName(unsigned symbolId);
unsigned symbolByteIndex(unsigned symbolId);
unsigned symbolCount();

void collectSymbols(ExprRef e, std::vector<unsigned> &out);
size_t exprNodeCount(ExprRef e);
std::string exprToString(ExprRef e);
uint64_t evaluateExpr(ExprRef e, const std::vector<uint8_t> &assignment);

namespace bv {

ExprRef constant(unsigned width, uint64_t value);
ExprRef symbol(unsigned symbolId);
ExprRef boolConst(bool value);
ExprRef truth();
ExprRef falsity();

ExprRef concat(ExprRef high, ExprRef low);
ExprRef extract(ExprRef a, unsigned offset, unsigned width);
ExprRef zext(ExprRef a, unsigned width);
ExprRef sext(ExprRef a, unsigned width);
ExprRef resize(ExprRef a, unsigned width, bool isSigned);

ExprRef add(ExprRef a, ExprRef b);
ExprRef sub(ExprRef a, ExprRef b);
ExprRef mul(ExprRef a, ExprRef b);
ExprRef udiv(ExprRef a, ExprRef b);
ExprRef sdiv(ExprRef a, ExprRef b);
ExprRef urem(ExprRef a, ExprRef b);
ExprRef srem(ExprRef a, ExprRef b);
ExprRef neg(ExprRef a);

ExprRef bitAnd(ExprRef a, ExprRef b);
ExprRef bitOr(ExprRef a, ExprRef b);
ExprRef bitXor(ExprRef a, ExprRef b);
ExprRef bitNot(ExprRef a);
ExprRef shl(ExprRef a, ExprRef b);
ExprRef lshr(ExprRef a, ExprRef b);
ExprRef ashr(ExprRef a, ExprRef b);

ExprRef eq(ExprRef a, ExprRef b);
ExprRef ne(ExprRef a, ExprRef b);
ExprRef ult(ExprRef a, ExprRef b);
ExprRef ule(ExprRef a, ExprRef b);
ExprRef ugt(ExprRef a, ExprRef b);
ExprRef uge(ExprRef a, ExprRef b);
ExprRef slt(ExprRef a, ExprRef b);
ExprRef sle(ExprRef a, ExprRef b);
ExprRef sgt(ExprRef a, ExprRef b);
ExprRef sge(ExprRef a, ExprRef b);

ExprRef logicalNot(ExprRef a);
ExprRef logicalAnd(ExprRef a, ExprRef b);
ExprRef logicalOr(ExprRef a, ExprRef b);
ExprRef ite(ExprRef cond, ExprRef whenTrue, ExprRef whenFalse);

ExprRef isZero(ExprRef a);
ExprRef isNonZero(ExprRef a);

}

}

#endif
