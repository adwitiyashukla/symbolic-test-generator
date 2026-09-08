#ifndef SYMX_EMIT_EMIT_H
#define SYMX_EMIT_EMIT_H

#include <cstdint>
#include <string>
#include <vector>

#include "exec/executor.h"

namespace symx {

std::string renderHarness(const std::string &prototype, const std::string &entryName,
                          const std::vector<InputRegion> &layout,
                          const std::vector<GeneratedTest> &tests);

std::string renderTestsJson(const std::string &sourcePath, const std::string &entryName,
                            const std::vector<InputRegion> &layout,
                            const std::vector<GeneratedTest> &tests);

std::string renderReport(const std::string &sourcePath, const std::string &entryName,
                         const Executor &executor, const Solver &solver);

std::string describeTestInputs(const std::vector<InputRegion> &layout,
                               const std::vector<uint8_t> &input);

std::string entryPrototype(const IrFunction &function, const std::vector<InputRegion> &layout);

}

#endif
