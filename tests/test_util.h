#ifndef SYMX_TESTS_TEST_UTIL_H
#define SYMX_TESTS_TEST_UTIL_H

#include <cstdio>
#include <exception>
#include <functional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace symxtest {

struct Failure {
  std::string message;
};

struct TestCase {
  std::string name;
  std::function<void()> body;
};

inline std::vector<TestCase> &registry() {
  static std::vector<TestCase> cases;
  return cases;
}

struct Registrar {
  Registrar(const std::string &name, std::function<void()> body) {
    registry().push_back({name, std::move(body)});
  }
};

inline void failCheck(const char *file, int line, const std::string &message) {
  std::ostringstream os;
  os << file << ":" << line << ": " << message;
  throw Failure{os.str()};
}

inline int runAll(const char *suite) {
  int failed = 0;
  for (const TestCase &test : registry()) {
    try {
      test.body();
      std::printf("  pass  %s\n", test.name.c_str());
    } catch (const Failure &failure) {
      std::printf("  FAIL  %s\n        %s\n", test.name.c_str(), failure.message.c_str());
      failed++;
    } catch (const std::exception &error) {
      std::printf("  FAIL  %s\n        unexpected exception: %s\n", test.name.c_str(),
                  error.what());
      failed++;
    }
  }
  std::printf("%s: %d of %d passed\n", suite, static_cast<int>(registry().size()) - failed,
              static_cast<int>(registry().size()));
  return failed == 0 ? 0 : 1;
}

}

#define SYMX_TEST(name)                                        \
  static void name();                                          \
  static symxtest::Registrar symxRegistrar##name(#name, name); \
  static void name()

#define CHECK(condition)                                   \
  do {                                                     \
    if (!(condition)) {                                    \
      symxtest::failCheck(__FILE__, __LINE__, #condition); \
    }                                                      \
  } while (0)

#define CHECK_EQ(actual, expected)                                                                 \
  do {                                                                                             \
    auto symxActual = (actual);                                                                    \
    auto symxExpected = (expected);                                                                \
    if (!(symxActual == symxExpected)) {                                                           \
      std::ostringstream symxStream;                                                               \
      symxStream << #actual << " == " << #expected << " (" << symxActual << " vs " << symxExpected \
                 << ")";                                                                           \
      symxtest::failCheck(__FILE__, __LINE__, symxStream.str());                                   \
    }                                                                                              \
  } while (0)

#define SYMX_TEST_MAIN(suite) \
  int main() { return symxtest::runAll(suite); }

#endif
