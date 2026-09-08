#include "emit/emit.h"

#include <sstream>

#include "util/support.h"

namespace symx {
namespace {

int64_t decodeScalar(const std::vector<uint8_t> &input, uint64_t offset, uint64_t size,
                     bool isSigned) {
  uint64_t value = 0;
  for (uint64_t i = 0; i < size; i++) {
    uint64_t byte = offset + i < input.size() ? input[offset + i] : 0;
    value |= byte << (8 * i);
  }
  if (isSigned) {
    return asSigned(value, static_cast<unsigned>(size * 8));
  }
  return static_cast<int64_t>(value);
}

const char *kHarnessPrologue = R"HARNESS(#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

#define SYMX_TIME_LIMIT_SECONDS 300

#ifdef _WIN32
static LONG WINAPI symx_fault_filter(EXCEPTION_POINTERS *info) {
  (void)info;
  TerminateProcess(GetCurrentProcess(), 3);
  return EXCEPTION_EXECUTE_HANDLER;
}

static DWORD WINAPI symx_watchdog(LPVOID unused) {
  (void)unused;
  Sleep(SYMX_TIME_LIMIT_SECONDS * 1000);
  TerminateProcess(GetCurrentProcess(), 124);
  return 0;
}
#endif

static void symx_guard(void) {
#ifdef _WIN32
  SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
  SetUnhandledExceptionFilter(symx_fault_filter);
  _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
  CreateThread(NULL, 0, symx_watchdog, NULL, 0, NULL);
#else
  alarm(SYMX_TIME_LIMIT_SECONDS);
#endif
}
)HARNESS";

std::string declarationFor(TypeRef type, const std::string &name, uint64_t elements,
                           bool isBuffer) {
  std::ostringstream os;
  if (isBuffer) {
    os << typeName(type->element) << " " << name << "[" << elements << "]";
  } else {
    os << typeName(type) << " " << name;
  }
  return os.str();
}

}

std::string entryPrototype(const IrFunction &function, const std::vector<InputRegion> &layout) {
  std::ostringstream os;
  os << typeName(function.returnType) << " " << function.name << "(";
  if (layout.empty()) {
    os << "void";
  }
  for (size_t i = 0; i < layout.size(); i++) {
    if (i > 0) {
      os << ", ";
    }
    os << typeName(function.parameterTypes[i]) << " " << layout[i].name;
  }
  os << ");";
  return os.str();
}

std::string describeTestInputs(const std::vector<InputRegion> &layout,
                               const std::vector<uint8_t> &input) {
  std::ostringstream os;
  for (size_t i = 0; i < layout.size(); i++) {
    const InputRegion &region = layout[i];
    if (i > 0) {
      os << ", ";
    }
    os << region.name << " = ";
    if (region.isBuffer) {
      TypeRef element = region.type->element;
      uint64_t elementSize = typeSizeInBytes(element);
      os << "{";
      for (uint64_t k = 0; k < region.elementCount; k++) {
        if (k > 0) {
          os << ", ";
        }
        os << decodeScalar(input, region.offset + k * elementSize, elementSize, element->isSigned);
      }
      os << "}";
    } else {
      os << decodeScalar(input, region.offset, region.size, region.type->isSigned);
    }
  }
  return os.str();
}

std::string renderHarness(const std::string &prototype, const std::string &entryName,
                          const std::vector<InputRegion> &layout,
                          const std::vector<GeneratedTest> &tests) {
  std::ostringstream os;
  uint64_t inputSize = 0;
  for (const InputRegion &region : layout) {
    inputSize = region.offset + region.size;
  }
  if (inputSize == 0) {
    inputSize = 1;
  }
  os << kHarnessPrologue;
  os << "\n" << prototype << "\n\n";
  os << "#define SYMX_TEST_COUNT " << (tests.empty() ? 1 : tests.size()) << "\n";
  os << "#define SYMX_INPUT_SIZE " << inputSize << "\n\n";
  os << "static const unsigned char symx_inputs[SYMX_TEST_COUNT][SYMX_INPUT_SIZE] = {\n";
  if (tests.empty()) {
    os << "    {0}\n";
  } else {
    for (const GeneratedTest &test : tests) {
      os << "    {";
      for (uint64_t i = 0; i < inputSize; i++) {
        if (i > 0) {
          os << ", ";
        }
        os << static_cast<int>(i < test.input.size() ? test.input[i] : 0);
      }
      os << "},\n";
    }
  }
  os << "};\n\n";
  os << "static long long symx_run_one(const unsigned char *data) {\n";
  os << "  (void)data;\n";
  for (const InputRegion &region : layout) {
    os << "  " << declarationFor(region.type, region.name, region.elementCount, region.isBuffer)
       << ";\n";
    if (region.isBuffer) {
      os << "  memcpy(" << region.name << ", data + " << region.offset << ", " << region.size
         << ");\n";
    } else {
      os << "  memcpy(&" << region.name << ", data + " << region.offset << ", " << region.size
         << ");\n";
    }
  }
  os << "  return (long long)" << entryName << "(";
  for (size_t i = 0; i < layout.size(); i++) {
    if (i > 0) {
      os << ", ";
    }
    os << layout[i].name;
  }
  os << ");\n";
  os << "}\n\n";
  os << "int main(int argc, char **argv) {\n";
  os << "  symx_guard();\n";
  os << "  int single = -1;\n";
  os << "  int start = 0;\n";
  os << "  if (argc > 2 && strcmp(argv[1], \"one\") == 0) {\n";
  os << "    single = atoi(argv[2]);\n";
  os << "  } else if (argc > 2 && strcmp(argv[1], \"from\") == 0) {\n";
  os << "    start = atoi(argv[2]);\n";
  os << "  }\n";
  os << "  for (int i = start; i < SYMX_TEST_COUNT; i++) {\n";
  os << "    if (single >= 0 && i != single) {\n";
  os << "      continue;\n";
  os << "    }\n";
  os << "    printf(\"symx test %d\\n\", i);\n";
  os << "    fflush(stdout);\n";
  os << "    long long result = symx_run_one(symx_inputs[i]);\n";
  os << "    printf(\"symx result %d %lld\\n\", i, result);\n";
  os << "    fflush(stdout);\n";
  os << "  }\n";
  os << "  return 0;\n";
  os << "}\n";
  return os.str();
}

std::string renderTestsJson(const std::string &sourcePath, const std::string &entryName,
                            const std::vector<InputRegion> &layout,
                            const std::vector<GeneratedTest> &tests) {
  std::ostringstream os;
  os << "{\n";
  os << "  \"source\": \"" << jsonEscape(sourcePath) << "\",\n";
  os << "  \"entry\": \"" << jsonEscape(entryName) << "\",\n";
  os << "  \"parameters\": [\n";
  for (size_t i = 0; i < layout.size(); i++) {
    const InputRegion &region = layout[i];
    os << "    {\"name\": \"" << jsonEscape(region.name) << "\", \"type\": \""
       << jsonEscape(typeName(region.type)) << "\", \"offset\": " << region.offset
       << ", \"size\": " << region.size << ", \"elements\": " << region.elementCount << "}";
    os << (i + 1 == layout.size() ? "\n" : ",\n");
  }
  os << "  ],\n";
  os << "  \"tests\": [\n";
  for (size_t i = 0; i < tests.size(); i++) {
    const GeneratedTest &test = tests[i];
    os << "    {\"index\": " << i << ", \"values\": \""
       << jsonEscape(describeTestInputs(layout, test.input)) << "\", \"bytes\": [";
    for (size_t k = 0; k < test.input.size(); k++) {
      if (k > 0) {
        os << ", ";
      }
      os << static_cast<int>(test.input[k]);
    }
    os << "], \"new_branches\": " << test.newBranches << ", \"path_length\": " << test.pathLength;
    if (test.isError) {
      os << ", \"error\": {\"kind\": \"" << errorKindName(test.errorKind)
         << "\", \"line\": " << test.location.line << ", \"column\": " << test.location.column
         << ", \"message\": \"" << jsonEscape(test.message) << "\"}";
    } else {
      os << ", \"error\": null";
    }
    os << "}";
    os << (i + 1 == tests.size() ? "\n" : ",\n");
  }
  os << "  ]\n";
  os << "}\n";
  return os.str();
}

std::string renderReport(const std::string &sourcePath, const std::string &entryName,
                         const Executor &executor, const Solver &solver) {
  std::ostringstream os;
  const ExplorationStats &stats = executor.stats();
  const SolverStats &solverStats = solver.stats();
  uint64_t covered = executor.coveredBranchCount();
  uint64_t total = executor.totalBranchSlots();
  os << "source            " << sourcePath << "\n";
  os << "entry             " << entryName << "\n";
  os << "input bytes       " << executor.inputSize() << "\n";
  os << "branch coverage   " << covered << " of " << total << " directions";
  if (total > 0) {
    os << " (" << (covered * 100 / total) << " percent)";
  }
  os << "\n";
  os << "lines reached     " << executor.coveredLines().size() << "\n";
  os << "paths completed   " << stats.completedPaths << "\n";
  os << "paths cut short   " << stats.limitedPaths << "\n";
  os << "forks             " << stats.forks << "\n";
  os << "instructions      " << stats.instructions << "\n";
  os << "tests written     " << executor.tests().size() << "\n";
  os << "errors found      " << stats.errors << "\n";
  os << "solver queries    " << solverStats.queries << "\n";
  os << "solver cache hits " << solverStats.cacheHits << "\n";
  os << "constraints cut   " << solverStats.constraintsDropped << "\n";
  os << "sat conflicts     " << solverStats.satConflicts << "\n";
  os << "cnf gates built   " << solverStats.gates << "\n";
  os << "solver seconds    " << solverStats.seconds << "\n";
  os << "total seconds     " << stats.seconds << "\n";
  if (stats.hitLimit) {
    os << "note              exploration stopped at a configured limit\n";
  }
  return os.str();
}

}
