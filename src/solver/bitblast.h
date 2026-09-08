#ifndef SYMX_SOLVER_BITBLAST_H
#define SYMX_SOLVER_BITBLAST_H

#include <cstdint>
#include <unordered_map>
#include <vector>

#include "solver/expr.h"
#include "solver/sat.h"

namespace symx {

class BitBlaster {
 public:
  explicit BitBlaster(SatSolver &solver);

  void assertTrue(ExprRef condition);
  std::vector<int> blast(ExprRef expression);

  const std::vector<unsigned> &usedSymbols() const { return usedSymbols_; }
  uint8_t symbolValue(unsigned symbolId) const;
  size_t gateCount() const { return gateCount_; }

 private:
  int trueLit() const { return trueLit_; }
  int falseLit() const { return litNegate(trueLit_); }
  bool isConstantLit(int lit) const { return lit == trueLit_ || lit == litNegate(trueLit_); }
  bool constantLitValue(int lit) const { return lit == trueLit_; }

  int freshLit();
  int gateAnd(int a, int b);
  int gateOr(int a, int b);
  int gateXor(int a, int b);
  int gateIte(int selector, int whenTrue, int whenFalse);

  std::vector<int> constantBits(uint64_t value, unsigned width) const;
  std::vector<int> symbolBits(unsigned symbolId);

  std::vector<int> buildAdd(const std::vector<int> &a, const std::vector<int> &b, int carryIn);
  std::vector<int> buildSub(const std::vector<int> &a, const std::vector<int> &b);
  std::vector<int> buildMul(const std::vector<int> &a, const std::vector<int> &b);
  std::vector<int> buildNegate(const std::vector<int> &a);
  std::vector<int> buildShift(const std::vector<int> &a, const std::vector<int> &amount, bool left,
                              bool arithmetic);
  void buildDivide(const std::vector<int> &a, const std::vector<int> &b, std::vector<int> &quotient,
                   std::vector<int> &remainder);
  std::vector<int> buildSignedDivide(const std::vector<int> &a, const std::vector<int> &b,
                                     bool wantRemainder);
  int buildEqual(const std::vector<int> &a, const std::vector<int> &b);
  int buildUnsignedLess(const std::vector<int> &a, const std::vector<int> &b, bool orEqual);
  int buildSignedLess(const std::vector<int> &a, const std::vector<int> &b, bool orEqual);
  int buildIsZero(const std::vector<int> &a);
  std::vector<int> buildMux(int selector, const std::vector<int> &whenTrue,
                            const std::vector<int> &whenFalse);

  SatSolver &solver_;
  int trueLit_ = 0;
  size_t gateCount_ = 0;
  std::unordered_map<ExprRef, std::vector<int>> cache_;
  std::unordered_map<unsigned, std::vector<int>> symbolCache_;
  std::vector<unsigned> usedSymbols_;
};

}

#endif
