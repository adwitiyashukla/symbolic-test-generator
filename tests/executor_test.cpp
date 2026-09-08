#include "exec/executor.h"

#include <memory>
#include <string>
#include <vector>

#include "ir/lower.h"
#include "solver/solver.h"
#include "tests/test_util.h"

namespace {

struct Session {
  symx::IrModule module;
  std::unique_ptr<symx::Solver> solver;
  std::unique_ptr<symx::Executor> executor;
  std::string error;
  bool ready = false;
};

std::unique_ptr<Session> explore(
    const std::string &source, symx::SearchStrategy strategy = symx::SearchStrategy::CoverageGuided,
    bool checkShifts = false) {
  std::unique_ptr<Session> session(new Session());
  if (!symx::compileSource(source, "snippet.c", session->module, session->error)) {
    return session;
  }
  symx::SolverOptions solverOptions;
  session->solver.reset(new symx::Solver(solverOptions));
  symx::ExecutorOptions options;
  options.entryFunction = "check";
  options.strategy = strategy;
  options.bufferElements = 4;
  options.maxSeconds = 30.0;
  options.checkShiftAmount = checkShifts;
  session->executor.reset(new symx::Executor(session->module, options, *session->solver));
  if (!session->executor->prepare(session->error)) {
    return session;
  }
  session->executor->explore();
  session->ready = true;
  return session;
}

uint64_t replayCoverage(const std::string &source, const std::vector<symx::GeneratedTest> &tests) {
  symx::IrModule module;
  std::string error;
  if (!symx::compileSource(source, "snippet.c", module, error)) {
    return 0;
  }
  symx::SolverOptions solverOptions;
  symx::Solver solver(solverOptions);
  symx::ExecutorOptions options;
  options.entryFunction = "check";
  options.bufferElements = 4;
  symx::Executor executor(module, options, solver);
  if (!executor.prepare(error)) {
    return 0;
  }
  for (const symx::GeneratedTest &test : tests) {
    executor.runConcrete(test.input);
  }
  return executor.coveredBranchCount();
}

bool hasError(const Session &session, symx::ErrorKind kind) {
  for (const symx::GeneratedTest &test : session.executor->tests()) {
    if (test.isError && test.errorKind == kind) {
      return true;
    }
  }
  return false;
}

const char *kClassify =
    "int check(int a, int b) {\n"
    "  if (a > 100) {\n"
    "    if (b < -5) {\n"
    "      return 1;\n"
    "    }\n"
    "    return 2;\n"
    "  }\n"
    "  if (a == b) {\n"
    "    return 3;\n"
    "  }\n"
    "  return 4;\n"
    "}\n";

SYMX_TEST(EveryBranchDirectionIsReached) {
  std::unique_ptr<Session> session = explore(kClassify);
  CHECK(session->ready);
  CHECK_EQ(session->executor->coveredBranchCount(), session->executor->totalBranchSlots());
  CHECK(session->executor->tests().size() >= 4);
}

SYMX_TEST(GeneratedTestsReplayToTheSameCoverage) {
  std::unique_ptr<Session> session = explore(kClassify);
  CHECK(session->ready);
  uint64_t replayed = replayCoverage(kClassify, session->executor->tests());
  CHECK_EQ(replayed, session->executor->coveredBranchCount());
}

SYMX_TEST(EverySearchStrategyReachesFullCoverage) {
  symx::SearchStrategy strategies[] = {
      symx::SearchStrategy::DepthFirst, symx::SearchStrategy::BreadthFirst,
      symx::SearchStrategy::Random, symx::SearchStrategy::CoverageGuided};
  for (symx::SearchStrategy strategy : strategies) {
    std::unique_ptr<Session> session = explore(kClassify, strategy);
    CHECK(session->ready);
    CHECK_EQ(session->executor->coveredBranchCount(), session->executor->totalBranchSlots());
  }
}

SYMX_TEST(AssertionFailuresAreFoundWithAWitness) {
  const char *source =
      "#include <assert.h>\n"
      "int check(int x) {\n"
      "  if (x * 4 == 40) {\n"
      "    assert(x != 10);\n"
      "  }\n"
      "  return 0;\n"
      "}\n";
  std::unique_ptr<Session> session = explore(source);
  CHECK(session->ready);
  CHECK(hasError(*session, symx::ErrorKind::Assertion));
  for (const symx::GeneratedTest &test : session->executor->tests()) {
    if (!test.isError) {
      continue;
    }
    int32_t value = 0;
    for (int i = 0; i < 4; i++) {
      value |= static_cast<int32_t>(test.input[i]) << (8 * i);
    }
    CHECK_EQ(value, 10);
  }
}

SYMX_TEST(DivisionByZeroIsReported) {
  const char *source = "int check(int a, int b) { return a / b; }";
  std::unique_ptr<Session> session = explore(source);
  CHECK(session->ready);
  CHECK(hasError(*session, symx::ErrorKind::DivideByZero));
  CHECK(hasError(*session, symx::ErrorKind::DivisionOverflow));
}

SYMX_TEST(OutOfBoundsAccessIsReported) {
  const char *source =
      "int check(int index, int values[4]) {\n"
      "  int table[4];\n"
      "  int i;\n"
      "  for (i = 0; i < 4; i++) {\n"
      "    table[i] = values[i];\n"
      "  }\n"
      "  return table[index];\n"
      "}\n";
  std::unique_ptr<Session> session = explore(source);
  CHECK(session->ready);
  CHECK(hasError(*session, symx::ErrorKind::OutOfBounds));
}

SYMX_TEST(NullPointerDereferenceIsReported) {
  const char *source =
      "int check(int x) {\n"
      "  int *pointer = 0;\n"
      "  if (x == 3) {\n"
      "    return *pointer;\n"
      "  }\n"
      "  return 0;\n"
      "}\n";
  std::unique_ptr<Session> session = explore(source);
  CHECK(session->ready);
  CHECK(hasError(*session, symx::ErrorKind::NullPointer));
}

SYMX_TEST(ShiftAmountCheckIsOptional) {
  const char *source = "int check(int value, int amount) { return value << amount; }";
  std::unique_ptr<Session> quiet = explore(source, symx::SearchStrategy::CoverageGuided, false);
  CHECK(quiet->ready);
  CHECK(!hasError(*quiet, symx::ErrorKind::ShiftAmount));
  std::unique_ptr<Session> strict = explore(source, symx::SearchStrategy::CoverageGuided, true);
  CHECK(strict->ready);
  CHECK(hasError(*strict, symx::ErrorKind::ShiftAmount));
}

SYMX_TEST(AssumptionsRuleOutInputs) {
  const char *source =
      "static void symx_assume(int condition) { (void)condition; }\n"
      "int check(int x) {\n"
      "  symx_assume(x > 1000);\n"
      "  if (x < 0) {\n"
      "    return 1;\n"
      "  }\n"
      "  return 2;\n"
      "}\n";
  std::unique_ptr<Session> session = explore(source);
  CHECK(session->ready);
  CHECK(session->executor->coveredBranchCount() < session->executor->totalBranchSlots());
  for (const symx::GeneratedTest &test : session->executor->tests()) {
    int32_t value = 0;
    for (int i = 0; i < 4; i++) {
      value |= static_cast<int32_t>(test.input[i]) << (8 * i);
    }
    CHECK(value > 1000);
  }
}

SYMX_TEST(DeepEqualityChainsAreSolved) {
  const char *source =
      "#include <assert.h>\n"
      "int check(unsigned char data[4]) {\n"
      "  unsigned int state = 305419896u;\n"
      "  int i;\n"
      "  for (i = 0; i < 4; i++) {\n"
      "    state = ((state << 7) | (state >> 25)) ^ data[i];\n"
      "  }\n"
      "  if (state == 2164507875u) {\n"
      "    assert(0);\n"
      "  }\n"
      "  return 0;\n"
      "}\n";
  std::unique_ptr<Session> session = explore(source);
  CHECK(session->ready);
  CHECK(hasError(*session, symx::ErrorKind::Assertion));
}

SYMX_TEST(TheQueryCacheIsUsed) {
  std::unique_ptr<Session> session = explore(kClassify);
  CHECK(session->ready);
  CHECK(session->solver->stats().queries > 0);
  CHECK(session->solver->stats().satisfiable + session->solver->stats().unsatisfiable +
            session->solver->stats().cacheHits ==
        session->solver->stats().queries);
}

SYMX_TEST(MissingEntryFunctionIsAnError) {
  symx::IrModule module;
  std::string error;
  CHECK(symx::compileSource("int other(void) { return 0; }", "snippet.c", module, error));
  symx::SolverOptions solverOptions;
  symx::Solver solver(solverOptions);
  symx::ExecutorOptions options;
  symx::Executor executor(module, options, solver);
  CHECK(!executor.prepare(error));
  CHECK(error.find("check") != std::string::npos);
}

}

SYMX_TEST_MAIN("executor_test")
