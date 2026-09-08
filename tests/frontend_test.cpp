#include <string>
#include <vector>

#include "exec/executor.h"
#include "ir/lower.h"
#include "solver/solver.h"
#include "tests/test_util.h"

namespace {

struct Outcome {
  bool compiled = false;
  bool completed = false;
  long long value = 0;
  std::string error;
};

Outcome run(const std::string &source, const std::vector<uint8_t> &input) {
  Outcome outcome;
  symx::IrModule module;
  if (!symx::compileSource(source, "snippet.c", module, outcome.error)) {
    return outcome;
  }
  outcome.compiled = true;
  symx::SolverOptions solverOptions;
  symx::Solver solver(solverOptions);
  symx::ExecutorOptions options;
  options.entryFunction = "check";
  options.bufferElements = 8;
  symx::Executor executor(module, options, solver);
  if (!executor.prepare(outcome.error)) {
    return outcome;
  }
  std::vector<uint8_t> padded = input;
  padded.resize(executor.inputSize(), 0);
  executor.runConcrete(padded);
  outcome.completed = executor.lastReturnDefined();
  outcome.value = static_cast<long long>(executor.lastReturnValue());
  return outcome;
}

std::vector<uint8_t> word(int32_t value) {
  std::vector<uint8_t> bytes(4);
  uint32_t raw = static_cast<uint32_t>(value);
  for (int i = 0; i < 4; i++) {
    bytes[i] = static_cast<uint8_t>((raw >> (8 * i)) & 0xff);
  }
  return bytes;
}

std::vector<uint8_t> words(const std::vector<int32_t> &values) {
  std::vector<uint8_t> bytes;
  for (int32_t value : values) {
    std::vector<uint8_t> part = word(value);
    bytes.insert(bytes.end(), part.begin(), part.end());
  }
  return bytes;
}

SYMX_TEST(ArithmeticFollowsPrecedence) {
  Outcome outcome = run("int check(void) { return 2 + 3 * 4 - 10 / 5; }", {});
  CHECK(outcome.compiled);
  CHECK(outcome.completed);
  CHECK_EQ(outcome.value, 12LL);
}

SYMX_TEST(SignedAndUnsignedDivisionDiffer) {
  Outcome signedCase = run("int check(void) { int a = -7; int b = 2; return a / b; }", {});
  CHECK_EQ(signedCase.value, -3LL);
  Outcome remainder = run("int check(void) { int a = -7; int b = 2; return a % b; }", {});
  CHECK_EQ(remainder.value, -1LL);
  Outcome unsignedCase =
      run("int check(void) { unsigned int a = 4294967289u; return (int)(a / 2u); }", {});
  CHECK_EQ(unsignedCase.value, 2147483644LL);
}

SYMX_TEST(CharPromotesToIntForArithmetic) {
  Outcome outcome = run("int check(void) { char a = 100; char b = 100; return a + b; }", {});
  CHECK_EQ(outcome.value, 200LL);
  Outcome truncated =
      run("int check(void) { char a = 100; char b = 100; char c = a + b; return c; }", {});
  CHECK_EQ(truncated.value, -56LL);
}

SYMX_TEST(ShiftsAndBitwiseOperatorsWork) {
  Outcome outcome =
      run("int check(void) { int a = 1; a = a << 10; a = a | 3; a = a ^ 1; a = a & 0x7ff; "
          "return a; }",
          {});
  CHECK_EQ(outcome.value, 1026LL);
  Outcome arithmeticShift = run("int check(void) { int a = -16; return a >> 2; }", {});
  CHECK_EQ(arithmeticShift.value, -4LL);
  Outcome logicalShift =
      run("int check(void) { unsigned int a = 4294967280u; return (int)(a >> 28); }", {});
  CHECK_EQ(logicalShift.value, 15LL);
}

SYMX_TEST(LoopsAccumulate) {
  Outcome forLoop =
      run("int check(int n) { int total = 0; int i; for (i = 1; i <= n; i++) { total += i; } "
          "return total; }",
          word(10));
  CHECK_EQ(forLoop.value, 55LL);
  Outcome whileLoop =
      run("int check(int n) { int total = 0; while (n > 0) { total = total + n; n = n - 1; } "
          "return total; }",
          word(5));
  CHECK_EQ(whileLoop.value, 15LL);
  Outcome doLoop =
      run("int check(int n) { int count = 0; do { count++; n = n / 2; } while (n > 0); "
          "return count; }",
          word(8));
  CHECK_EQ(doLoop.value, 4LL);
}

SYMX_TEST(BreakAndContinueChangeControlFlow) {
  Outcome outcome =
      run("int check(int n) { int total = 0; int i; for (i = 0; i < n; i++) { if (i % 3 == 0) "
          "{ continue; } if (i > 7) { break; } total += i; } return total; }",
          word(20));
  CHECK_EQ(outcome.value, 19LL);
}

SYMX_TEST(ArraysAndPointerArithmeticAgree) {
  Outcome outcome =
      run("int check(void) { int values[4]; int i; for (i = 0; i < 4; i++) { values[i] = i * i; } "
          "int *cursor = values; cursor = cursor + 2; return *cursor + values[3]; }",
          {});
  CHECK_EQ(outcome.value, 13LL);
  Outcome difference =
      run("int check(void) { int values[4]; int *a = values; int *b = &values[3]; return (int)(b - "
          "a); }",
          {});
  CHECK_EQ(difference.value, 3LL);
}

SYMX_TEST(TwoDimensionalArraysIndexCorrectly) {
  Outcome outcome =
      run("int check(void) { int grid[3][3]; int r; int c; for (r = 0; r < 3; r++) { "
          "for (c = 0; c < 3; c++) { grid[r][c] = r * 3 + c; } } return grid[2][1]; }",
          {});
  CHECK_EQ(outcome.value, 7LL);
}

SYMX_TEST(ParameterBuffersAreReadable) {
  Outcome outcome =
      run("int check(int data[4]) { return data[0] + data[3]; }", words({5, 6, 7, 9}));
  CHECK_EQ(outcome.value, 14LL);
}

SYMX_TEST(FunctionCallsAndRecursionWork) {
  Outcome outcome =
      run("static int factorial(int n) { if (n <= 1) { return 1; } return n * factorial(n - 1); } "
          "int check(int n) { return factorial(n); }",
          word(6));
  CHECK_EQ(outcome.value, 720LL);
}

SYMX_TEST(GlobalsKeepTheirInitialValues) {
  Outcome outcome =
      run("int counter = 41; int table[3] = {7, 8, 9}; "
          "int check(void) { counter = counter + table[2]; return counter; }",
          {});
  CHECK_EQ(outcome.value, 50LL);
}

SYMX_TEST(ShortCircuitOperatorsSkipTheSecondOperand) {
  Outcome outcome =
      run("int calls = 0; static int bump(void) { calls = calls + 1; return 1; } "
          "int check(void) { if (0 && bump()) { } if (1 || bump()) { } return calls; }",
          {});
  CHECK_EQ(outcome.value, 0LL);
  Outcome evaluated =
      run("int calls = 0; static int bump(void) { calls = calls + 1; return 1; } "
          "int check(void) { if (1 && bump()) { } if (0 || bump()) { } return calls; }",
          {});
  CHECK_EQ(evaluated.value, 2LL);
}

SYMX_TEST(ConditionalExpressionPicksOneSide) {
  Outcome outcome = run("int check(int n) { return n > 0 ? n * 2 : -n; }", word(-9));
  CHECK_EQ(outcome.value, 9LL);
  Outcome positive = run("int check(int n) { return n > 0 ? n * 2 : -n; }", word(9));
  CHECK_EQ(positive.value, 18LL);
}

SYMX_TEST(IncrementAndCompoundAssignmentUpdateInPlace) {
  Outcome outcome =
      run("int check(void) { int a = 5; int b = a++; int c = ++a; a *= 2; a -= 3; a <<= 1; "
          "return a + b + c; }",
          {});
  CHECK_EQ(outcome.value, 34LL);
}

SYMX_TEST(SizeofAndCastsBehave) {
  Outcome outcome = run("int check(void) { return (int)sizeof(int) + (int)sizeof(char); }", {});
  CHECK_EQ(outcome.value, 5LL);
  Outcome narrowing = run("int check(void) { int big = 300; return (char)big; }", {});
  CHECK_EQ(narrowing.value, 44LL);
}

SYMX_TEST(DefinesBecomeConstants) {
  Outcome outcome = run("#define LIMIT 12\nint check(void) { return LIMIT * 2; }", {});
  CHECK_EQ(outcome.value, 24LL);
}

SYMX_TEST(ParseErrorsAreReported) {
  Outcome outcome = run("int check(void) { return 1 + ; }", {});
  CHECK(!outcome.compiled);
  CHECK(outcome.error.find("snippet.c") != std::string::npos);
  Outcome unknown = run("int check(void) { return missing; }", {});
  CHECK(!unknown.compiled);
  CHECK(unknown.error.find("unknown identifier") != std::string::npos);
}

}

SYMX_TEST_MAIN("frontend_test")
