#include "solver/bitblast.h"

#include <algorithm>

#include "util/support.h"

namespace symx {

BitBlaster::BitBlaster(SatSolver &solver) : solver_(solver) {
  int var = solver_.newVar();
  trueLit_ = litFromVar(var, false);
  solver_.addUnit(trueLit_);
}

int BitBlaster::freshLit() {
  gateCount_++;
  return litFromVar(solver_.newVar(), false);
}

int BitBlaster::gateAnd(int a, int b) {
  if (isConstantLit(a)) {
    return constantLitValue(a) ? b : falseLit();
  }
  if (isConstantLit(b)) {
    return constantLitValue(b) ? a : falseLit();
  }
  if (a == b) {
    return a;
  }
  if (a == litNegate(b)) {
    return falseLit();
  }
  int out = freshLit();
  solver_.addClause({litNegate(a), litNegate(b), out});
  solver_.addClause({a, litNegate(out)});
  solver_.addClause({b, litNegate(out)});
  return out;
}

int BitBlaster::gateOr(int a, int b) { return litNegate(gateAnd(litNegate(a), litNegate(b))); }

int BitBlaster::gateXor(int a, int b) {
  if (isConstantLit(a)) {
    return constantLitValue(a) ? litNegate(b) : b;
  }
  if (isConstantLit(b)) {
    return constantLitValue(b) ? litNegate(a) : a;
  }
  if (a == b) {
    return falseLit();
  }
  if (a == litNegate(b)) {
    return trueLit();
  }
  int out = freshLit();
  solver_.addClause({litNegate(a), litNegate(b), litNegate(out)});
  solver_.addClause({a, b, litNegate(out)});
  solver_.addClause({a, litNegate(b), out});
  solver_.addClause({litNegate(a), b, out});
  return out;
}

int BitBlaster::gateIte(int selector, int whenTrue, int whenFalse) {
  if (isConstantLit(selector)) {
    return constantLitValue(selector) ? whenTrue : whenFalse;
  }
  if (whenTrue == whenFalse) {
    return whenTrue;
  }
  if (isConstantLit(whenTrue) && isConstantLit(whenFalse)) {
    return constantLitValue(whenTrue) ? selector : litNegate(selector);
  }
  int out = freshLit();
  solver_.addClause({litNegate(selector), litNegate(whenTrue), out});
  solver_.addClause({litNegate(selector), whenTrue, litNegate(out)});
  solver_.addClause({selector, litNegate(whenFalse), out});
  solver_.addClause({selector, whenFalse, litNegate(out)});
  return out;
}

std::vector<int> BitBlaster::constantBits(uint64_t value, unsigned width) const {
  std::vector<int> bits(width);
  for (unsigned i = 0; i < width; i++) {
    bits[i] = ((value >> i) & 1ULL) ? trueLit_ : litNegate(trueLit_);
  }
  return bits;
}

std::vector<int> BitBlaster::symbolBits(unsigned symbolId) {
  auto found = symbolCache_.find(symbolId);
  if (found != symbolCache_.end()) {
    return found->second;
  }
  std::vector<int> bits(8);
  for (int i = 0; i < 8; i++) {
    bits[i] = litFromVar(solver_.newVar(), false);
  }
  symbolCache_.emplace(symbolId, bits);
  usedSymbols_.push_back(symbolId);
  return bits;
}

uint8_t BitBlaster::symbolValue(unsigned symbolId) const {
  auto found = symbolCache_.find(symbolId);
  if (found == symbolCache_.end()) {
    return 0;
  }
  uint8_t value = 0;
  for (int i = 0; i < 8; i++) {
    if (solver_.modelValue(litVar(found->second[i]))) {
      value |= static_cast<uint8_t>(1u << i);
    }
  }
  return value;
}

std::vector<int> BitBlaster::buildAdd(const std::vector<int> &a, const std::vector<int> &b,
                                      int carryIn) {
  std::vector<int> sum(a.size());
  int carry = carryIn;
  for (size_t i = 0; i < a.size(); i++) {
    int partial = gateXor(a[i], b[i]);
    sum[i] = gateXor(partial, carry);
    int carryFromInputs = gateAnd(a[i], b[i]);
    int carryFromPartial = gateAnd(partial, carry);
    carry = gateOr(carryFromInputs, carryFromPartial);
  }
  return sum;
}

std::vector<int> BitBlaster::buildSub(const std::vector<int> &a, const std::vector<int> &b) {
  std::vector<int> inverted(b.size());
  for (size_t i = 0; i < b.size(); i++) {
    inverted[i] = litNegate(b[i]);
  }
  return buildAdd(a, inverted, trueLit());
}

std::vector<int> BitBlaster::buildNegate(const std::vector<int> &a) {
  std::vector<int> zero(a.size(), falseLit());
  return buildSub(zero, a);
}

std::vector<int> BitBlaster::buildMul(const std::vector<int> &a, const std::vector<int> &b) {
  size_t width = a.size();
  std::vector<int> accumulator(width, falseLit());
  for (size_t i = 0; i < width; i++) {
    if (isConstantLit(b[i]) && !constantLitValue(b[i])) {
      continue;
    }
    std::vector<int> partial(width, falseLit());
    for (size_t k = i; k < width; k++) {
      partial[k] = gateAnd(a[k - i], b[i]);
    }
    accumulator = buildAdd(accumulator, partial, falseLit());
  }
  return accumulator;
}

std::vector<int> BitBlaster::buildMux(int selector, const std::vector<int> &whenTrue,
                                      const std::vector<int> &whenFalse) {
  std::vector<int> result(whenTrue.size());
  for (size_t i = 0; i < whenTrue.size(); i++) {
    result[i] = gateIte(selector, whenTrue[i], whenFalse[i]);
  }
  return result;
}

std::vector<int> BitBlaster::buildShift(const std::vector<int> &a, const std::vector<int> &amount,
                                        bool left, bool arithmetic) {
  size_t width = a.size();
  int fill = arithmetic ? a[width - 1] : falseLit();
  std::vector<int> current = a;
  unsigned stages = 0;
  while ((1u << stages) < width) {
    stages++;
  }
  for (unsigned stage = 0; stage < stages && stage < amount.size(); stage++) {
    size_t distance = static_cast<size_t>(1) << stage;
    std::vector<int> shifted(width);
    for (size_t i = 0; i < width; i++) {
      if (left) {
        shifted[i] = i >= distance ? current[i - distance] : falseLit();
      } else {
        shifted[i] = i + distance < width ? current[i + distance] : fill;
      }
    }
    current = buildMux(amount[stage], shifted, current);
  }
  int overflow = falseLit();
  for (size_t i = stages; i < amount.size(); i++) {
    overflow = gateOr(overflow, amount[i]);
  }
  std::vector<int> saturated(width, left ? falseLit() : fill);
  return buildMux(overflow, saturated, current);
}

int BitBlaster::buildIsZero(const std::vector<int> &a) {
  int anySet = falseLit();
  for (int bit : a) {
    anySet = gateOr(anySet, bit);
  }
  return litNegate(anySet);
}

int BitBlaster::buildEqual(const std::vector<int> &a, const std::vector<int> &b) {
  int equal = trueLit();
  for (size_t i = 0; i < a.size(); i++) {
    equal = gateAnd(equal, litNegate(gateXor(a[i], b[i])));
  }
  return equal;
}

int BitBlaster::buildUnsignedLess(const std::vector<int> &a, const std::vector<int> &b,
                                  bool orEqual) {
  int result = orEqual ? trueLit() : falseLit();
  for (size_t i = 0; i < a.size(); i++) {
    int less = gateAnd(litNegate(a[i]), b[i]);
    int equal = litNegate(gateXor(a[i], b[i]));
    result = gateOr(less, gateAnd(equal, result));
  }
  return result;
}

int BitBlaster::buildSignedLess(const std::vector<int> &a, const std::vector<int> &b,
                                bool orEqual) {
  std::vector<int> flippedA = a;
  std::vector<int> flippedB = b;
  flippedA[a.size() - 1] = litNegate(a[a.size() - 1]);
  flippedB[b.size() - 1] = litNegate(b[b.size() - 1]);
  return buildUnsignedLess(flippedA, flippedB, orEqual);
}

void BitBlaster::buildDivide(const std::vector<int> &a, const std::vector<int> &b,
                             std::vector<int> &quotient, std::vector<int> &remainder) {
  size_t width = a.size();
  std::vector<int> extendedB = b;
  extendedB.push_back(falseLit());
  std::vector<int> current(width + 1, falseLit());
  quotient.assign(width, falseLit());
  for (size_t step = 0; step < width; step++) {
    size_t index = width - 1 - step;
    for (size_t i = width; i > 0; i--) {
      current[i] = current[i - 1];
    }
    current[0] = a[index];
    std::vector<int> difference = buildSub(current, extendedB);
    int canSubtract = litNegate(buildUnsignedLess(current, extendedB, false));
    current = buildMux(canSubtract, difference, current);
    quotient[index] = canSubtract;
  }
  remainder.assign(width, falseLit());
  for (size_t i = 0; i < width; i++) {
    remainder[i] = current[i];
  }
  int divisorIsZero = buildIsZero(b);
  std::vector<int> allOnes(width, trueLit());
  quotient = buildMux(divisorIsZero, allOnes, quotient);
  remainder = buildMux(divisorIsZero, a, remainder);
}

std::vector<int> BitBlaster::buildSignedDivide(const std::vector<int> &a, const std::vector<int> &b,
                                               bool wantRemainder) {
  size_t width = a.size();
  int signA = a[width - 1];
  int signB = b[width - 1];
  std::vector<int> magnitudeA = buildMux(signA, buildNegate(a), a);
  std::vector<int> magnitudeB = buildMux(signB, buildNegate(b), b);
  std::vector<int> quotient;
  std::vector<int> remainder;
  buildDivide(magnitudeA, magnitudeB, quotient, remainder);
  int divisorIsZero = buildIsZero(b);
  if (wantRemainder) {
    std::vector<int> signedRemainder = buildMux(signA, buildNegate(remainder), remainder);
    return buildMux(divisorIsZero, a, signedRemainder);
  }
  int negateResult = gateXor(signA, signB);
  std::vector<int> signedQuotient = buildMux(negateResult, buildNegate(quotient), quotient);
  std::vector<int> one = constantBits(1, static_cast<unsigned>(width));
  std::vector<int> allOnes(width, trueLit());
  std::vector<int> zeroDivisorResult = buildMux(signA, one, allOnes);
  return buildMux(divisorIsZero, zeroDivisorResult, signedQuotient);
}

void BitBlaster::assertTrue(ExprRef condition) {
  std::vector<int> bits = blast(condition);
  solver_.addUnit(bits[0]);
}

std::vector<int> BitBlaster::blast(ExprRef expression) {
  auto found = cache_.find(expression);
  if (found != cache_.end()) {
    return found->second;
  }
  std::vector<int> result;
  unsigned width = expression->width;
  switch (expression->kind) {
    case ExprKind::Constant:
      result = constantBits(expression->value, width);
      break;
    case ExprKind::Symbol:
      result = symbolBits(static_cast<unsigned>(expression->value));
      break;
    case ExprKind::Concat: {
      std::vector<int> high = blast(expression->a);
      std::vector<int> low = blast(expression->b);
      result = low;
      result.insert(result.end(), high.begin(), high.end());
      break;
    }
    case ExprKind::Extract: {
      std::vector<int> source = blast(expression->a);
      unsigned offset = static_cast<unsigned>(expression->value);
      result.assign(source.begin() + offset, source.begin() + offset + width);
      break;
    }
    case ExprKind::ZExt: {
      result = blast(expression->a);
      result.resize(width, falseLit());
      break;
    }
    case ExprKind::SExt: {
      std::vector<int> source = blast(expression->a);
      int sign = source[source.size() - 1];
      result = source;
      result.resize(width, sign);
      break;
    }
    case ExprKind::Add:
      result = buildAdd(blast(expression->a), blast(expression->b), falseLit());
      break;
    case ExprKind::Sub:
      result = buildSub(blast(expression->a), blast(expression->b));
      break;
    case ExprKind::Mul:
      result = buildMul(blast(expression->a), blast(expression->b));
      break;
    case ExprKind::UDiv: {
      std::vector<int> quotient;
      std::vector<int> remainder;
      buildDivide(blast(expression->a), blast(expression->b), quotient, remainder);
      result = quotient;
      break;
    }
    case ExprKind::URem: {
      std::vector<int> quotient;
      std::vector<int> remainder;
      buildDivide(blast(expression->a), blast(expression->b), quotient, remainder);
      result = remainder;
      break;
    }
    case ExprKind::SDiv:
      result = buildSignedDivide(blast(expression->a), blast(expression->b), false);
      break;
    case ExprKind::SRem:
      result = buildSignedDivide(blast(expression->a), blast(expression->b), true);
      break;
    case ExprKind::And: {
      std::vector<int> a = blast(expression->a);
      std::vector<int> b = blast(expression->b);
      result.resize(width);
      for (unsigned i = 0; i < width; i++) {
        result[i] = gateAnd(a[i], b[i]);
      }
      break;
    }
    case ExprKind::Or: {
      std::vector<int> a = blast(expression->a);
      std::vector<int> b = blast(expression->b);
      result.resize(width);
      for (unsigned i = 0; i < width; i++) {
        result[i] = gateOr(a[i], b[i]);
      }
      break;
    }
    case ExprKind::Xor: {
      std::vector<int> a = blast(expression->a);
      std::vector<int> b = blast(expression->b);
      result.resize(width);
      for (unsigned i = 0; i < width; i++) {
        result[i] = gateXor(a[i], b[i]);
      }
      break;
    }
    case ExprKind::Shl:
      result = buildShift(blast(expression->a), blast(expression->b), true, false);
      break;
    case ExprKind::LShr:
      result = buildShift(blast(expression->a), blast(expression->b), false, false);
      break;
    case ExprKind::AShr:
      result = buildShift(blast(expression->a), blast(expression->b), false, true);
      break;
    case ExprKind::Not:
      result.push_back(litNegate(blast(expression->a)[0]));
      break;
    case ExprKind::Eq:
      result.push_back(buildEqual(blast(expression->a), blast(expression->b)));
      break;
    case ExprKind::Ult:
      result.push_back(buildUnsignedLess(blast(expression->a), blast(expression->b), false));
      break;
    case ExprKind::Ule:
      result.push_back(buildUnsignedLess(blast(expression->a), blast(expression->b), true));
      break;
    case ExprKind::Slt:
      result.push_back(buildSignedLess(blast(expression->a), blast(expression->b), false));
      break;
    case ExprKind::Sle:
      result.push_back(buildSignedLess(blast(expression->a), blast(expression->b), true));
      break;
    case ExprKind::Ite: {
      int selector = blast(expression->a)[0];
      result = buildMux(selector, blast(expression->b), blast(expression->c));
      break;
    }
  }
  if (result.size() != width) {
    fatal("bit blast width mismatch for " + std::string(exprKindName(expression->kind)));
  }
  cache_.emplace(expression, result);
  return result;
}

}
