#ifndef SYMX_CLI_VERIFY_H
#define SYMX_CLI_VERIFY_H

#include <cstdint>
#include <string>
#include <vector>

#include "emit/emit.h"
#include "exec/executor.h"
#include "ir/ir.h"

namespace symx {

struct ExpectedResult {
  bool defined = false;
  long long value = 0;
};

struct CoverageNumbers {
  double linePercent = -1.0;
  double branchPercent = -1.0;
  int lineTotal = 0;
  int branchTotal = 0;
};

struct VerifyReport {
  std::string compiler;
  bool compiled = false;
  int testsRun = 0;
  int errorTests = 0;
  int reproducedErrors = 0;
  int unexpectedFailures = 0;
  int randomInputs = 0;
  int randomFailures = 0;
  int returnAgreements = 0;
  int returnDisagreements = 0;
  int returnSkipped = 0;
  CoverageNumbers symbolic;
  CoverageNumbers random;
  std::string note;
};

bool findCompiler(std::string &compiler);

bool verifyWithCompiler(const std::string &sourcePath, const std::string &entryName,
                        const std::string &prototype, const std::vector<InputRegion> &layout,
                        const std::vector<GeneratedTest> &tests,
                        const std::vector<std::vector<uint8_t>> &randomInputs,
                        const std::vector<ExpectedResult> &expectedResults,
                        const std::string &outputDirectory, VerifyReport &report,
                        std::string &error);

}

#endif
