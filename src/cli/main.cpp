#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "cli/verify.h"
#include "emit/emit.h"
#include "exec/executor.h"
#include "ir/lower.h"
#include "solver/sat.h"
#include "solver/solver.h"
#include "util/config.h"
#include "util/support.h"

namespace symx {
namespace {

struct CommandLine {
  std::string command;
  std::string path;
  std::string entry = "check";
  std::string outputDirectory = "symx-out";
  std::string configPath = "config.toml";
  std::string strategy;
  int64_t randomCount = -1;
  int64_t bufferElements = -1;
  int64_t seed = -1;
  double maxSeconds = -1.0;
};

void printUsage() {
  std::cout << "usage: symx <command> [options]\n\n";
  std::cout << "commands:\n";
  std::cout << "  explore <file.c>   generate tests that reach every branch it can\n";
  std::cout << "  verify <file.c>    generate tests, then compile and run them with the system"
               " compiler\n";
  std::cout << "  random <file.c>    measure what random inputs reach, for comparison\n";
  std::cout << "  ir <file.c>        print the intermediate representation\n";
  std::cout << "  sat <file.cnf>     solve a DIMACS CNF file with the built in solver\n\n";
  std::cout << "options:\n";
  std::cout << "  --entry <name>     entry function, default check\n";
  std::cout << "  --out <dir>        output directory, default symx-out\n";
  std::cout << "  --config <file>    settings file, default config.toml\n";
  std::cout << "  --strategy <name>  dfs, bfs, random or coverage\n";
  std::cout << "  --seconds <n>      exploration time budget\n";
  std::cout << "  --count <n>        number of random inputs\n";
  std::cout << "  --buffer <n>       elements allocated for each pointer parameter\n";
  std::cout << "  --seed <n>         random seed\n";
}

bool parseArguments(int argc, char **argv, CommandLine &line, std::string &error) {
  if (argc < 2) {
    error = "missing command";
    return false;
  }
  line.command = argv[1];
  int index = 2;
  if (index < argc && argv[index][0] != '-') {
    line.path = argv[index];
    index++;
  }
  for (; index < argc; index++) {
    std::string option = argv[index];
    bool needsValue = option != "--help";
    std::string value;
    if (needsValue) {
      if (index + 1 >= argc) {
        error = "option " + option + " needs a value";
        return false;
      }
      value = argv[index + 1];
      index++;
    }
    if (option == "--entry") {
      line.entry = value;
    } else if (option == "--out") {
      line.outputDirectory = value;
    } else if (option == "--config") {
      line.configPath = value;
    } else if (option == "--strategy") {
      line.strategy = value;
    } else if (option == "--seconds") {
      line.maxSeconds = std::strtod(value.c_str(), nullptr);
    } else if (option == "--count") {
      line.randomCount = std::strtoll(value.c_str(), nullptr, 10);
    } else if (option == "--buffer") {
      line.bufferElements = std::strtoll(value.c_str(), nullptr, 10);
    } else if (option == "--seed") {
      line.seed = std::strtoll(value.c_str(), nullptr, 10);
    } else {
      error = "unknown option " + option;
      return false;
    }
  }
  return true;
}

bool buildModule(const CommandLine &line, IrModule &module, std::string &error) {
  if (line.path.empty()) {
    error = "missing input file";
    return false;
  }
  if (!fileExists(line.path)) {
    error = "cannot open " + line.path;
    return false;
  }
  std::string source = readFileOrDie(line.path);
  return compileSource(source, line.path, module, error);
}

bool buildSettings(const CommandLine &line, Settings &settings, std::string &error) {
  if (!loadSettings(line.configPath, settings, error)) {
    return false;
  }
  if (!line.strategy.empty()) {
    SearchStrategy parsed = SearchStrategy::CoverageGuided;
    if (!parseSearchStrategy(line.strategy, parsed)) {
      error = "unknown search strategy " + line.strategy;
      return false;
    }
    settings.strategy = line.strategy;
  }
  if (line.maxSeconds > 0) {
    settings.maxSeconds = line.maxSeconds;
  }
  if (line.randomCount > 0) {
    settings.randomCount = line.randomCount;
  }
  if (line.bufferElements > 0) {
    settings.bufferElements = line.bufferElements;
  }
  if (line.seed >= 0) {
    settings.seed = line.seed;
  }
  return true;
}

ExecutorOptions makeExecutorOptions(const Settings &settings, const CommandLine &line) {
  ExecutorOptions options;
  options.entryFunction = line.entry;
  parseSearchStrategy(settings.strategy, options.strategy);
  options.bufferElements = static_cast<uint64_t>(settings.bufferElements);
  options.maxSteps = static_cast<uint64_t>(settings.maxSteps);
  options.maxStates = static_cast<uint64_t>(settings.maxStates);
  options.maxForksPerPath = static_cast<uint64_t>(settings.maxForksPerPath);
  options.maxCallDepth = static_cast<uint64_t>(settings.maxCallDepth);
  options.maxTests = static_cast<uint64_t>(settings.maxTests);
  options.symbolicOffsetLimit = static_cast<uint64_t>(settings.symbolicOffsetLimit);
  options.maxSeconds = settings.maxSeconds;
  options.seed = static_cast<uint64_t>(settings.seed);
  options.checkShiftAmount = settings.checkShiftAmount;
  return options;
}

SolverOptions makeSolverOptions(const Settings &settings) {
  SolverOptions options;
  options.useCache = settings.solverCache;
  options.useIndependence = settings.solverIndependence;
  options.conflictBudget = settings.solverConflictBudget;
  return options;
}

void printErrors(const Executor &executor) {
  bool any = false;
  for (const GeneratedTest &test : executor.tests()) {
    if (!test.isError) {
      continue;
    }
    if (!any) {
      std::cout << "\nerrors\n";
      any = true;
    }
    std::cout << "  line " << test.location.line << " column " << test.location.column << ": "
              << errorKindName(test.errorKind) << ", " << test.message << "\n";
    std::cout << "    input " << describeTestInputs(executor.inputLayout(), test.input) << "\n";
  }
  if (!any) {
    std::cout << "\nno errors found\n";
  }
}

void printTests(const Executor &executor) {
  std::cout << "\ngenerated tests\n";
  for (size_t i = 0; i < executor.tests().size(); i++) {
    const GeneratedTest &test = executor.tests()[i];
    std::cout << "  " << i << "  " << describeTestInputs(executor.inputLayout(), test.input);
    if (test.isError) {
      std::cout << "   [" << errorKindName(test.errorKind) << " at line " << test.location.line
                << "]";
    }
    std::cout << "\n";
  }
}

bool writeOutputs(const CommandLine &line, const Executor &executor, const IrModule &module,
                  std::string &prototype, std::string &error) {
  if (!makeDirectory(line.outputDirectory)) {
    error = "cannot create " + line.outputDirectory;
    return false;
  }
  prototype = entryPrototype(executor.entry(), executor.inputLayout());
  std::string harness =
      renderHarness(prototype, line.entry, executor.inputLayout(), executor.tests());
  std::string json =
      renderTestsJson(module.fileName, line.entry, executor.inputLayout(), executor.tests());
  if (!writeFile(joinPath(line.outputDirectory, "harness.c"), harness) ||
      !writeFile(joinPath(line.outputDirectory, "tests.json"), json)) {
    error = "cannot write results into " + line.outputDirectory;
    return false;
  }
  return true;
}

std::vector<std::vector<uint8_t>> makeRandomInputs(uint64_t count, uint64_t size, uint64_t seed) {
  Rng rng(seed);
  std::vector<std::vector<uint8_t>> inputs;
  inputs.reserve(count);
  for (uint64_t i = 0; i < count; i++) {
    std::vector<uint8_t> input(size, 0);
    for (uint64_t k = 0; k < size; k++) {
      uint64_t roll = rng.below(10);
      if (roll == 0) {
        input[k] = 0;
      } else if (roll == 1) {
        input[k] = 255;
      } else {
        input[k] = static_cast<uint8_t>(rng.below(256));
      }
    }
    inputs.push_back(input);
  }
  return inputs;
}

int commandExplore(const CommandLine &line, const Settings &settings) {
  IrModule module;
  std::string error;
  if (!buildModule(line, module, error)) {
    std::cerr << error << "\n";
    return 1;
  }
  Solver solver(makeSolverOptions(settings));
  Executor executor(module, makeExecutorOptions(settings, line), solver);
  if (!executor.prepare(error)) {
    std::cerr << error << "\n";
    return 1;
  }
  executor.explore();
  std::string prototype;
  if (!writeOutputs(line, executor, module, prototype, error)) {
    std::cerr << error << "\n";
    return 1;
  }
  std::cout << renderReport(module.fileName, line.entry, executor, solver);
  printErrors(executor);
  printTests(executor);
  std::cout << "\nwrote " << joinPath(line.outputDirectory, "harness.c") << " and "
            << joinPath(line.outputDirectory, "tests.json") << "\n";
  return 0;
}

int commandRandom(const CommandLine &line, const Settings &settings) {
  IrModule module;
  std::string error;
  if (!buildModule(line, module, error)) {
    std::cerr << error << "\n";
    return 1;
  }
  Solver solver(makeSolverOptions(settings));
  Executor executor(module, makeExecutorOptions(settings, line), solver);
  if (!executor.prepare(error)) {
    std::cerr << error << "\n";
    return 1;
  }
  uint64_t count = static_cast<uint64_t>(settings.randomCount);
  std::vector<std::vector<uint8_t>> inputs =
      makeRandomInputs(count, executor.inputSize(), static_cast<uint64_t>(settings.seed));
  Timer timer;
  for (const std::vector<uint8_t> &input : inputs) {
    executor.runConcrete(input);
  }
  double seconds = timer.seconds();
  uint64_t covered = executor.coveredBranchCount();
  uint64_t total = executor.totalBranchSlots();
  std::cout << "source            " << module.fileName << "\n";
  std::cout << "entry             " << line.entry << "\n";
  std::cout << "random inputs     " << count << "\n";
  std::cout << "branch coverage   " << covered << " of " << total << " directions";
  if (total > 0) {
    std::cout << " (" << (covered * 100 / total) << " percent)";
  }
  std::cout << "\n";
  std::cout << "lines reached     " << executor.coveredLines().size() << "\n";
  std::cout << "errors found      " << executor.stats().errors << "\n";
  std::cout << "seconds           " << seconds << "\n";
  return 0;
}

int commandVerify(const CommandLine &line, const Settings &settings) {
  IrModule module;
  std::string error;
  if (!buildModule(line, module, error)) {
    std::cerr << error << "\n";
    return 1;
  }
  Solver solver(makeSolverOptions(settings));
  Executor executor(module, makeExecutorOptions(settings, line), solver);
  if (!executor.prepare(error)) {
    std::cerr << error << "\n";
    return 1;
  }
  executor.explore();
  std::string prototype;
  if (!writeOutputs(line, executor, module, prototype, error)) {
    std::cerr << error << "\n";
    return 1;
  }
  std::cout << renderReport(module.fileName, line.entry, executor, solver);
  printErrors(executor);
  std::vector<std::vector<uint8_t>> randomInputs =
      makeRandomInputs(static_cast<uint64_t>(settings.randomCount), executor.inputSize(),
                       static_cast<uint64_t>(settings.seed));
  std::vector<ExpectedResult> expectedResults;
  expectedResults.reserve(randomInputs.size());
  for (const std::vector<uint8_t> &input : randomInputs) {
    executor.runConcrete(input);
    ExpectedResult expected;
    expected.defined = executor.lastReturnDefined();
    expected.value = static_cast<long long>(executor.lastReturnValue());
    expectedResults.push_back(expected);
  }
  VerifyReport report;
  if (!verifyWithCompiler(line.path, line.entry, prototype, executor.inputLayout(),
                          executor.tests(), randomInputs, expectedResults, line.outputDirectory,
                          report, error)) {
    std::cerr << "\nverification could not run: " << error << "\n";
    return 1;
  }
  std::cout << "\nverification with " << report.compiler << "\n";
  std::cout << "  generated tests run     " << report.testsRun << "\n";
  std::cout << "  predicted errors        " << report.errorTests << "\n";
  std::cout << "  errors reproduced       " << report.reproducedErrors << "\n";
  std::cout << "  unexpected failures     " << report.unexpectedFailures << "\n";
  std::cout << "  gcov lines, generated   " << report.symbolic.linePercent << " percent of "
            << report.symbolic.lineTotal << "\n";
  std::cout << "  gcov branches, generated " << report.symbolic.branchPercent << " percent of "
            << report.symbolic.branchTotal << "\n";
  std::cout << "  random inputs run       " << report.randomInputs << "\n";
  std::cout << "  gcov lines, random      " << report.random.linePercent << " percent of "
            << report.random.lineTotal << "\n";
  std::cout << "  gcov branches, random   " << report.random.branchPercent << " percent of "
            << report.random.branchTotal << "\n";
  std::cout << "  random inputs that fail " << report.randomFailures << "\n";
  std::cout << "  return values matched   " << report.returnAgreements << "\n";
  std::cout << "  return values differed  " << report.returnDisagreements << "\n";
  std::cout << "  return values skipped   " << report.returnSkipped << "\n";
  bool ok = report.compiled && report.unexpectedFailures == 0 &&
            report.reproducedErrors == report.errorTests && report.returnDisagreements == 0;
  std::cout << "\nresult            "
            << (ok ? "the compiler agrees with the engine" : "the compiler and the engine disagree")
            << "\n";
  return ok ? 0 : 1;
}

int commandIr(const CommandLine &line) {
  IrModule module;
  std::string error;
  if (!buildModule(line, module, error)) {
    std::cerr << error << "\n";
    return 1;
  }
  for (const IrFunction &function : module.functions) {
    std::cout << dumpFunction(module, function) << "\n";
  }
  return 0;
}

int commandSat(const CommandLine &line) {
  if (line.path.empty() || !fileExists(line.path)) {
    std::cerr << "cannot open " << line.path << "\n";
    return 1;
  }
  SatSolver solver;
  int variables = 0;
  std::string error;
  if (!parseDimacs(readFileOrDie(line.path), solver, variables, error)) {
    std::cerr << error << "\n";
    return 1;
  }
  Timer timer;
  SatResult result = solver.solve();
  double seconds = timer.seconds();
  std::cout << "variables         " << variables << "\n";
  std::cout << "result            "
            << (result == SatResult::Sat
                    ? "satisfiable"
                    : (result == SatResult::Unsat ? "unsatisfiable" : "unknown"))
            << "\n";
  std::cout << "decisions         " << solver.stats().decisions << "\n";
  std::cout << "conflicts         " << solver.stats().conflicts << "\n";
  std::cout << "propagations      " << solver.stats().propagations << "\n";
  std::cout << "restarts          " << solver.stats().restarts << "\n";
  std::cout << "clauses removed   " << solver.stats().removedClauses << "\n";
  std::cout << "seconds           " << seconds << "\n";
  return result == SatResult::Unknown ? 1 : 0;
}

}
}

int main(int argc, char **argv) {
  symx::CommandLine line;
  std::string error;
  if (!symx::parseArguments(argc, argv, line, error)) {
    std::cerr << error << "\n\n";
    symx::printUsage();
    return 2;
  }
  if (line.command == "help" || line.command == "--help") {
    symx::printUsage();
    return 0;
  }
  if (line.command == "ir") {
    return symx::commandIr(line);
  }
  if (line.command == "sat") {
    return symx::commandSat(line);
  }
  symx::Settings settings;
  if (!symx::buildSettings(line, settings, error)) {
    std::cerr << error << "\n";
    return 2;
  }
  if (line.command == "explore") {
    return symx::commandExplore(line, settings);
  }
  if (line.command == "random") {
    return symx::commandRandom(line, settings);
  }
  if (line.command == "verify") {
    return symx::commandVerify(line, settings);
  }
  std::cerr << "unknown command " << line.command << "\n\n";
  symx::printUsage();
  return 2;
}
