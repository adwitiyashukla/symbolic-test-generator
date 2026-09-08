#include "solver/solver.h"

#include <vector>

#include "solver/expr.h"
#include "tests/test_util.h"
#include "util/support.h"

namespace {

using symx::ExprRef;
using symx::QueryResult;
using symx::Solver;
using symx::SolverOptions;

ExprRef randomValue(symx::Rng &rng, const std::vector<ExprRef> &leaves, int depth) {
  if (depth == 0 || rng.below(100) < 30) {
    if (rng.below(100) < 30) {
      return symx::bv::constant(8, rng.below(256));
    }
    return leaves[rng.below(static_cast<uint32_t>(leaves.size()))];
  }
  ExprRef a = randomValue(rng, leaves, depth - 1);
  ExprRef b = randomValue(rng, leaves, depth - 1);
  switch (rng.below(10)) {
    case 0:
      return symx::bv::add(a, b);
    case 1:
      return symx::bv::sub(a, b);
    case 2:
      return symx::bv::mul(a, b);
    case 3:
      return symx::bv::bitAnd(a, b);
    case 4:
      return symx::bv::bitOr(a, b);
    case 5:
      return symx::bv::bitXor(a, b);
    case 6:
      return symx::bv::shl(a, symx::bv::constant(8, rng.below(9)));
    case 7:
      return symx::bv::lshr(a, symx::bv::constant(8, rng.below(9)));
    case 8:
      return symx::bv::udiv(a, b);
    default:
      return symx::bv::urem(a, b);
  }
}

ExprRef randomCondition(symx::Rng &rng, const std::vector<ExprRef> &leaves) {
  ExprRef a = randomValue(rng, leaves, 3);
  ExprRef b = randomValue(rng, leaves, 3);
  switch (rng.below(6)) {
    case 0:
      return symx::bv::eq(a, b);
    case 1:
      return symx::bv::ne(a, b);
    case 2:
      return symx::bv::ult(a, b);
    case 3:
      return symx::bv::ule(a, b);
    case 4:
      return symx::bv::slt(a, b);
    default:
      return symx::bv::sle(a, b);
  }
}

bool bruteForceSatisfiable(const std::vector<ExprRef> &constraints, ExprRef condition,
                           unsigned symbolBase, unsigned symbolTotal) {
  std::vector<uint8_t> assignment(symx::symbolCount(), 0);
  unsigned count = 1u << (8 * symbolTotal);
  for (unsigned value = 0; value < count; value++) {
    for (unsigned i = 0; i < symbolTotal; i++) {
      assignment[symbolBase + i] = static_cast<uint8_t>((value >> (8 * i)) & 0xff);
    }
    bool ok = symx::evaluateExpr(condition, assignment) == 1;
    for (ExprRef constraint : constraints) {
      if (!ok) {
        break;
      }
      ok = symx::evaluateExpr(constraint, assignment) == 1;
    }
    if (ok) {
      return true;
    }
  }
  return false;
}

SYMX_TEST(ModelsAgreeWithBruteForceOnRandomQueries) {
  symx::Rng rng(4242);
  unsigned base = symx::addSymbolByte("brute", 0);
  unsigned second = symx::addSymbolByte("brute", 1);
  CHECK_EQ(second, base + 1);
  std::vector<ExprRef> leaves{symx::bv::symbol(base), symx::bv::symbol(second)};
  Solver solver;
  int satCount = 0;
  int unsatCount = 0;
  for (int trial = 0; trial < 120; trial++) {
    std::vector<ExprRef> constraints;
    int constraintCount = static_cast<int>(rng.below(3));
    for (int i = 0; i < constraintCount; i++) {
      constraints.push_back(randomCondition(rng, leaves));
    }
    ExprRef condition = randomCondition(rng, leaves);
    std::vector<uint8_t> assignment;
    QueryResult result = solver.solveForModel(constraints, condition, assignment);
    bool expected = bruteForceSatisfiable(constraints, condition, base, 2);
    CHECK((result == QueryResult::Sat) == expected);
    if (result == QueryResult::Sat) {
      satCount++;
      CHECK_EQ(symx::evaluateExpr(condition, assignment), 1ULL);
      for (ExprRef constraint : constraints) {
        CHECK_EQ(symx::evaluateExpr(constraint, assignment), 1ULL);
      }
    } else {
      unsatCount++;
    }
  }
  CHECK(satCount > 0);
  CHECK(unsatCount > 0);
}

SYMX_TEST(SignedAndDivisionOperatorsMatchEvaluation) {
  symx::Rng rng(9182);
  unsigned base = symx::addSymbolByte("divs", 0);
  std::vector<ExprRef> leaves{symx::bv::symbol(base)};
  Solver solver;
  for (int trial = 0; trial < 60; trial++) {
    ExprRef a = randomValue(rng, leaves, 2);
    ExprRef b = randomValue(rng, leaves, 2);
    ExprRef expression;
    switch (trial % 4) {
      case 0:
        expression = symx::bv::sdiv(a, b);
        break;
      case 1:
        expression = symx::bv::srem(a, b);
        break;
      case 2:
        expression = symx::bv::ashr(a, b);
        break;
      default:
        expression = symx::bv::udiv(a, b);
        break;
    }
    ExprRef target = symx::bv::constant(8, rng.below(256));
    ExprRef condition = symx::bv::eq(expression, target);
    std::vector<uint8_t> assignment;
    QueryResult result = solver.solveForModel({}, condition, assignment);
    bool expected = false;
    std::vector<uint8_t> probe(symx::symbolCount(), 0);
    for (unsigned value = 0; value < 256 && !expected; value++) {
      probe[base] = static_cast<uint8_t>(value);
      expected = symx::evaluateExpr(condition, probe) == 1;
    }
    CHECK((result == QueryResult::Sat) == expected);
    if (result == QueryResult::Sat) {
      CHECK_EQ(symx::evaluateExpr(condition, assignment), 1ULL);
    }
  }
}

SYMX_TEST(CacheAndIndependenceDoNotChangeAnswers) {
  symx::Rng rng(31337);
  unsigned first = symx::addSymbolByte("shared", 0);
  unsigned other = symx::addSymbolByte("unrelated", 0);
  std::vector<ExprRef> leaves{symx::bv::symbol(first)};
  std::vector<ExprRef> otherLeaves{symx::bv::symbol(other)};
  SolverOptions plain;
  plain.useCache = false;
  plain.useIndependence = false;
  Solver plainSolver(plain);
  Solver optimized;
  int compared = 0;
  for (int trial = 0; trial < 120; trial++) {
    std::vector<ExprRef> constraints;
    constraints.push_back(randomCondition(rng, leaves));
    constraints.push_back(randomCondition(rng, otherLeaves));
    if (plainSolver.check(constraints, symx::bv::truth()) != QueryResult::Sat) {
      continue;
    }
    ExprRef condition = randomCondition(rng, leaves);
    QueryResult expected = plainSolver.check(constraints, condition);
    QueryResult actual = optimized.check(constraints, condition);
    CHECK(expected == actual);
    compared++;
  }
  CHECK(compared > 20);
  CHECK(optimized.stats().constraintsDropped > 0);
  CHECK(optimized.stats().cacheHits > 0);
}

}

SYMX_TEST_MAIN("solver_test")
