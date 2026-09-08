#include "solver/expr.h"

#include <vector>

#include "tests/test_util.h"
#include "util/support.h"

namespace {

using symx::ExprRef;

ExprRef c8(uint64_t value) { return symx::bv::constant(8, value); }
ExprRef c32(uint64_t value) { return symx::bv::constant(32, value); }

SYMX_TEST(ConstantsFoldForArithmetic) {
  CHECK_EQ(symx::constantValue(symx::bv::add(c8(200), c8(100))), 44ULL);
  CHECK_EQ(symx::constantValue(symx::bv::sub(c8(3), c8(5))), 254ULL);
  CHECK_EQ(symx::constantValue(symx::bv::mul(c8(16), c8(16))), 0ULL);
  CHECK_EQ(symx::constantValue(symx::bv::udiv(c8(200), c8(7))), 28ULL);
  CHECK_EQ(symx::constantValue(symx::bv::urem(c8(200), c8(7))), 4ULL);
  CHECK_EQ(symx::constantValue(symx::bv::shl(c8(1), c8(9))), 0ULL);
  CHECK_EQ(symx::constantValue(symx::bv::lshr(c8(128), c8(7))), 1ULL);
  CHECK_EQ(symx::constantValue(symx::bv::ashr(c8(128), c8(7))), 255ULL);
  CHECK_EQ(symx::constantValue(symx::bv::udiv(c8(5), c8(0))), 255ULL);
  CHECK_EQ(symx::constantValue(symx::bv::urem(c8(5), c8(0))), 5ULL);
}

SYMX_TEST(IdentitiesRemoveNodes) {
  unsigned id = symx::addSymbolByte("identities", 0);
  ExprRef x = symx::bv::symbol(id);
  CHECK(symx::bv::add(x, c8(0)) == x);
  CHECK(symx::bv::mul(x, c8(1)) == x);
  CHECK(symx::constantValue(symx::bv::mul(x, c8(0))) == 0ULL);
  CHECK(symx::bv::bitOr(x, c8(0)) == x);
  CHECK(symx::bv::bitAnd(x, c8(255)) == x);
  CHECK(symx::constantValue(symx::bv::bitXor(x, x)) == 0ULL);
  CHECK(symx::isTrue(symx::bv::eq(x, x)));
  CHECK(symx::isFalse(symx::bv::ult(x, x)));
  CHECK(symx::bv::logicalNot(symx::bv::logicalNot(symx::bv::eq(x, c8(1)))) ==
        symx::bv::eq(x, c8(1)));
}

SYMX_TEST(HashConsingSharesStructure) {
  unsigned id = symx::addSymbolByte("shared", 0);
  ExprRef x = symx::bv::symbol(id);
  ExprRef first = symx::bv::add(symx::bv::mul(x, c8(3)), c8(7));
  ExprRef second = symx::bv::add(symx::bv::mul(x, c8(3)), c8(7));
  CHECK(first == second);
  CHECK(symx::exprNodeCount(first) == symx::exprNodeCount(second));
}

SYMX_TEST(ExtractOfConcatPicksTheRightHalf) {
  unsigned low = symx::addSymbolByte("halves", 0);
  unsigned high = symx::addSymbolByte("halves", 1);
  ExprRef combined = symx::bv::concat(symx::bv::symbol(high), symx::bv::symbol(low));
  CHECK(symx::bv::extract(combined, 0, 8) == symx::bv::symbol(low));
  CHECK(symx::bv::extract(combined, 8, 8) == symx::bv::symbol(high));
  CHECK(symx::bv::concat(symx::bv::extract(combined, 8, 8), symx::bv::extract(combined, 0, 8)) ==
        combined);
}

SYMX_TEST(MultiplyByConstantBecomesShiftsAndAdds) {
  unsigned id = symx::addSymbolByte("strength", 0);
  ExprRef x = symx::bv::zext(symx::bv::symbol(id), 32);
  ExprRef byTen = symx::bv::mul(x, c32(10));
  CHECK(byTen->kind != symx::ExprKind::Mul);
  std::vector<uint8_t> assignment(symx::symbolCount(), 0);
  assignment[id] = 37;
  CHECK_EQ(symx::evaluateExpr(byTen, assignment), 370ULL);
}

SYMX_TEST(EvaluationMatchesNativeArithmetic) {
  symx::Rng rng(2026);
  unsigned first = symx::addSymbolByte("native", 0);
  unsigned second = symx::addSymbolByte("native", 1);
  ExprRef a = symx::bv::zext(symx::bv::symbol(first), 32);
  ExprRef b = symx::bv::zext(symx::bv::symbol(second), 32);
  std::vector<uint8_t> assignment(symx::symbolCount(), 0);
  for (int trial = 0; trial < 500; trial++) {
    uint32_t left = rng.below(256);
    uint32_t right = rng.below(256);
    assignment[first] = static_cast<uint8_t>(left);
    assignment[second] = static_cast<uint8_t>(right);
    CHECK_EQ(symx::evaluateExpr(symx::bv::add(a, b), assignment), left + right);
    CHECK_EQ(symx::evaluateExpr(symx::bv::mul(a, b), assignment),
             static_cast<uint64_t>(left * right));
    CHECK_EQ(symx::evaluateExpr(symx::bv::sub(a, b), assignment),
             static_cast<uint64_t>(static_cast<uint32_t>(left - right)));
    if (right != 0) {
      CHECK_EQ(symx::evaluateExpr(symx::bv::udiv(a, b), assignment), left / right);
      CHECK_EQ(symx::evaluateExpr(symx::bv::urem(a, b), assignment), left % right);
    }
    CHECK_EQ(symx::evaluateExpr(symx::bv::ult(a, b), assignment), left < right ? 1ULL : 0ULL);
  }
}

SYMX_TEST(SignedComparisonsUseTwosComplement) {
  ExprRef negative = c32(0xffffffffULL);
  ExprRef positive = c32(1);
  CHECK(symx::isTrue(symx::bv::slt(negative, positive)));
  CHECK(symx::isFalse(symx::bv::ult(negative, positive)));
  CHECK_EQ(symx::asSigned(0xffffffffULL, 32), -1LL);
  CHECK_EQ(symx::signExtendFrom(0x80ULL, 8), 0xffffffffffffff80ULL);
}

}

SYMX_TEST_MAIN("expr_test")
