#include "solver/sat.h"

#include <vector>

#include "tests/test_util.h"
#include "util/support.h"

namespace {

using symx::litFromVar;
using symx::litNegated;
using symx::litVar;
using symx::SatResult;
using symx::SatSolver;

bool bruteForceSatisfiable(int variables, const std::vector<std::vector<int>> &clauses) {
  for (uint32_t assignment = 0; assignment < (1u << variables); assignment++) {
    bool ok = true;
    for (const std::vector<int> &clause : clauses) {
      bool satisfied = false;
      for (int lit : clause) {
        bool value = ((assignment >> litVar(lit)) & 1u) != 0;
        if (value != litNegated(lit)) {
          satisfied = true;
          break;
        }
      }
      if (!satisfied) {
        ok = false;
        break;
      }
    }
    if (ok) {
      return true;
    }
  }
  return false;
}

bool modelSatisfies(const SatSolver &solver, const std::vector<std::vector<int>> &clauses) {
  for (const std::vector<int> &clause : clauses) {
    bool satisfied = false;
    for (int lit : clause) {
      bool value = solver.modelValue(litVar(lit));
      if (value != litNegated(lit)) {
        satisfied = true;
        break;
      }
    }
    if (!satisfied) {
      return false;
    }
  }
  return true;
}

SYMX_TEST(EmptyProblemIsSatisfiable) {
  SatSolver solver;
  CHECK(solver.solve() == SatResult::Sat);
}

SYMX_TEST(ContradictoryUnitsAreUnsatisfiable) {
  SatSolver solver;
  int a = solver.newVar();
  solver.addClause({litFromVar(a, false)});
  solver.addClause({litFromVar(a, true)});
  CHECK(solver.solve() == SatResult::Unsat);
}

SYMX_TEST(PigeonholeThreeIntoTwoIsUnsatisfiable) {
  SatSolver solver;
  int holes = 2;
  int pigeons = 3;
  std::vector<std::vector<int>> variables(pigeons, std::vector<int>(holes));
  for (int p = 0; p < pigeons; p++) {
    for (int h = 0; h < holes; h++) {
      variables[p][h] = solver.newVar();
    }
  }
  for (int p = 0; p < pigeons; p++) {
    std::vector<int> clause;
    for (int h = 0; h < holes; h++) {
      clause.push_back(litFromVar(variables[p][h], false));
    }
    solver.addClause(clause);
  }
  for (int h = 0; h < holes; h++) {
    for (int p = 0; p < pigeons; p++) {
      for (int q = p + 1; q < pigeons; q++) {
        solver.addClause({litFromVar(variables[p][h], true), litFromVar(variables[q][h], true)});
      }
    }
  }
  CHECK(solver.solve() == SatResult::Unsat);
}

SYMX_TEST(RandomFormulasMatchBruteForce) {
  symx::Rng rng(20260908);
  int agreements = 0;
  for (int trial = 0; trial < 400; trial++) {
    int variables = 4 + static_cast<int>(rng.below(7));
    int clauseCount = 2 + static_cast<int>(rng.below(static_cast<uint32_t>(variables * 4)));
    std::vector<std::vector<int>> clauses;
    for (int c = 0; c < clauseCount; c++) {
      std::vector<int> clause;
      int width = 1 + static_cast<int>(rng.below(3));
      for (int k = 0; k < width; k++) {
        int var = static_cast<int>(rng.below(static_cast<uint32_t>(variables)));
        clause.push_back(litFromVar(var, (rng.next() & 1) != 0));
      }
      clauses.push_back(clause);
    }
    SatSolver solver;
    for (int i = 0; i < variables; i++) {
      solver.newVar();
    }
    for (const std::vector<int> &clause : clauses) {
      solver.addClause(clause);
    }
    SatResult result = solver.solve();
    bool expected = bruteForceSatisfiable(variables, clauses);
    CHECK((result == SatResult::Sat) == expected);
    if (result == SatResult::Sat) {
      CHECK(modelSatisfies(solver, clauses));
    }
    agreements++;
  }
  CHECK_EQ(agreements, 400);
}

SYMX_TEST(LargerRandomFormulasProduceValidModels) {
  symx::Rng rng(777);
  for (int trial = 0; trial < 20; trial++) {
    int variables = 60;
    int clauseCount = 240;
    std::vector<std::vector<int>> clauses;
    for (int c = 0; c < clauseCount; c++) {
      std::vector<int> clause;
      while (clause.size() < 3) {
        int var = static_cast<int>(rng.below(static_cast<uint32_t>(variables)));
        int lit = litFromVar(var, (rng.next() & 1) != 0);
        bool duplicate = false;
        for (int existing : clause) {
          if (litVar(existing) == var) {
            duplicate = true;
          }
        }
        if (!duplicate) {
          clause.push_back(lit);
        }
      }
      clauses.push_back(clause);
    }
    SatSolver solver;
    for (int i = 0; i < variables; i++) {
      solver.newVar();
    }
    for (const std::vector<int> &clause : clauses) {
      solver.addClause(clause);
    }
    if (solver.solve() == SatResult::Sat) {
      CHECK(modelSatisfies(solver, clauses));
    }
  }
}

SYMX_TEST(AssumptionsRestrictTheSearch) {
  SatSolver solver;
  int a = solver.newVar();
  int b = solver.newVar();
  solver.addClause({litFromVar(a, false), litFromVar(b, false)});
  CHECK(solver.solve({litFromVar(a, true), litFromVar(b, true)}) == SatResult::Unsat);
  CHECK(solver.solve({litFromVar(a, true)}) == SatResult::Sat);
  CHECK(solver.modelValue(b));
}

}

SYMX_TEST_MAIN("sat_test")
