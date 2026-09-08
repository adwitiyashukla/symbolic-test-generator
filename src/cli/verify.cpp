#include "cli/verify.h"

#include <cstdio>
#include <cstdlib>
#include <sstream>

#include "util/support.h"

#ifdef _WIN32
#define SYMX_POPEN _popen
#define SYMX_PCLOSE _pclose
#else
#define SYMX_POPEN popen
#define SYMX_PCLOSE pclose
#endif

namespace symx {
namespace {

int runCommand(const std::string &command, std::string &output) {
  output.clear();
  std::string full = command + " 2>&1";
  FILE *pipe = SYMX_POPEN(full.c_str(), "r");
  if (pipe == nullptr) {
    return -1;
  }
  char buffer[512];
  while (std::fgets(buffer, sizeof(buffer), pipe) != nullptr) {
    output += buffer;
  }
  int status = SYMX_PCLOSE(pipe);
#ifndef _WIN32
  if (status > 255) {
    status = status >> 8;
  }
#endif
  return status;
}

std::string quote(const std::string &text) { return "\"" + text + "\""; }

std::string inDirectory(const std::string &directory, const std::string &command) {
#ifdef _WIN32
  return "cd /d " + quote(directory) + " && " + command;
#else
  return "cd " + quote(directory) + " && " + command;
#endif
}

std::string buildCommands(const std::string &compiler, const std::string &flags) {
  std::ostringstream os;
  os << compiler << " " << flags << " -c target.c -o target.o && ";
  os << compiler << " " << flags << " -c harness.c -o harness.o && ";
  os << compiler << " -fprofile-arcs -o harness target.o harness.o";
  return os.str();
}

std::string harnessCommand(const std::string &mode, int index) {
  std::ostringstream os;
#ifdef _WIN32
  os << "harness " << mode << " " << index;
#else
  os << "./harness " << mode << " " << index;
#endif
  return os.str();
}

void compareResults(const std::string &transcript, const std::vector<ExpectedResult> &expected,
                    VerifyReport &report) {
  for (const std::string &line : splitLines(transcript)) {
    if (!startsWith(line, "symx result ")) {
      continue;
    }
    std::istringstream values(line.substr(12));
    int index = -1;
    long long produced = 0;
    values >> index >> produced;
    if (index < 0 || index >= static_cast<int>(expected.size())) {
      continue;
    }
    if (!expected[index].defined) {
      report.returnSkipped++;
    } else if (expected[index].value == produced) {
      report.returnAgreements++;
    } else {
      report.returnDisagreements++;
    }
  }
}

int lastReportedIndex(const std::string &output) {
  int last = -1;
  for (const std::string &line : splitLines(output)) {
    if (startsWith(line, "symx test ")) {
      last = std::atoi(line.c_str() + 10);
    }
  }
  return last;
}

bool parseGcovLine(const std::string &line, const std::string &prefix, double &percent,
                   int &total) {
  if (!startsWith(line, prefix)) {
    return false;
  }
  std::string rest = line.substr(prefix.size());
  size_t percentSign = rest.find('%');
  if (percentSign == std::string::npos) {
    return false;
  }
  percent = std::strtod(rest.substr(0, percentSign).c_str(), nullptr);
  size_t of = rest.find("of ");
  if (of != std::string::npos) {
    total = std::atoi(rest.c_str() + of + 3);
  }
  return true;
}

CoverageNumbers parseGcovOutput(const std::string &output, const std::string &sourceName) {
  CoverageNumbers numbers;
  std::vector<std::string> lines = splitLines(output);
  bool inTarget = false;
  for (const std::string &line : lines) {
    if (startsWith(line, "File ")) {
      inTarget = line.find(sourceName) != std::string::npos;
      continue;
    }
    if (!inTarget) {
      continue;
    }
    parseGcovLine(line, "Lines executed:", numbers.linePercent, numbers.lineTotal);
    parseGcovLine(line, "Taken at least once:", numbers.branchPercent, numbers.branchTotal);
  }
  return numbers;
}

bool prepareDirectory(const std::string &directory, const std::string &sourcePath,
                      const std::string &harness, std::string &error) {
  if (!makeDirectory(directory)) {
    error = "cannot create " + directory;
    return false;
  }
  std::string source = readFileOrDie(sourcePath);
  if (!writeFile(joinPath(directory, "target.c"), source)) {
    error = "cannot write target.c into " + directory;
    return false;
  }
  if (!writeFile(joinPath(directory, "harness.c"), harness)) {
    error = "cannot write harness.c into " + directory;
    return false;
  }
  return true;
}

}

bool findCompiler(std::string &compiler) {
  const char *fromEnvironment = std::getenv("CC");
  std::vector<std::string> candidates;
  if (fromEnvironment != nullptr && fromEnvironment[0] != '\0') {
    candidates.push_back(fromEnvironment);
  }
  candidates.push_back("cc");
  candidates.push_back("gcc");
  candidates.push_back("clang");
  std::string output;
  for (const std::string &candidate : candidates) {
    if (runCommand(candidate + " --version", output) == 0) {
      compiler = candidate;
      return true;
    }
  }
  return false;
}

bool verifyWithCompiler(const std::string &sourcePath, const std::string &entryName,
                        const std::string &prototype, const std::vector<InputRegion> &layout,
                        const std::vector<GeneratedTest> &tests,
                        const std::vector<std::vector<uint8_t>> &randomInputs,
                        const std::vector<ExpectedResult> &expectedResults,
                        const std::string &outputDirectory, VerifyReport &report,
                        std::string &error) {
  if (!findCompiler(report.compiler)) {
    error = "no C compiler found on PATH";
    return false;
  }
  std::string flags = "-std=c11 -O0 -g -fprofile-arcs -ftest-coverage";

  std::string symbolicDirectory = joinPath(outputDirectory, "check-generated");
  std::string symbolicHarness = renderHarness(prototype, entryName, layout, tests);
  if (!prepareDirectory(symbolicDirectory, sourcePath, symbolicHarness, error)) {
    return false;
  }
  std::string output;
  int status =
      runCommand(inDirectory(symbolicDirectory, buildCommands(report.compiler, flags)), output);
  if (status != 0) {
    error = "compiling the generated harness failed: " + output;
    return false;
  }
  report.compiled = true;
  for (size_t i = 0; i < tests.size(); i++) {
    int result = runCommand(
        inDirectory(symbolicDirectory, harnessCommand("one", static_cast<int>(i))), output);
    report.testsRun++;
    if (tests[i].isError) {
      report.errorTests++;
      if (result != 0) {
        report.reproducedErrors++;
      }
    } else if (result != 0) {
      report.unexpectedFailures++;
    }
  }
  runCommand(inDirectory(symbolicDirectory, "gcov -b target.c"), output);
  report.symbolic = parseGcovOutput(output, "target.c");

  if (!randomInputs.empty()) {
    std::vector<GeneratedTest> randomTests;
    randomTests.reserve(randomInputs.size());
    for (const std::vector<uint8_t> &input : randomInputs) {
      GeneratedTest test;
      test.input = input;
      randomTests.push_back(test);
    }
    std::string randomDirectory = joinPath(outputDirectory, "check-random");
    std::string randomHarness = renderHarness(prototype, entryName, layout, randomTests);
    if (!prepareDirectory(randomDirectory, sourcePath, randomHarness, error)) {
      return false;
    }
    status =
        runCommand(inDirectory(randomDirectory, buildCommands(report.compiler, flags)), output);
    if (status != 0) {
      error = "compiling the random harness failed: " + output;
      return false;
    }
    int next = 0;
    int guard = 0;
    std::string transcript;
    while (next < static_cast<int>(randomTests.size()) && guard < 8192) {
      guard++;
      int result = runCommand(inDirectory(randomDirectory, harnessCommand("from", next)), output);
      transcript += output;
      if (result == 0) {
        break;
      }
      report.randomFailures++;
      int failed = lastReportedIndex(output);
      next = failed < next ? next + 1 : failed + 1;
    }
    compareResults(transcript, expectedResults, report);
    report.randomInputs = static_cast<int>(randomTests.size());
    runCommand(inDirectory(randomDirectory, "gcov -b target.c"), output);
    report.random = parseGcovOutput(output, "target.c");
  }
  return true;
}

}
