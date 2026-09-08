#ifndef SYMX_IR_LOWER_H
#define SYMX_IR_LOWER_H

#include <cstdint>
#include <string>

#include "ir/ast.h"
#include "ir/ir.h"

namespace symx {

bool lowerProgram(const Program &program, IrModule &module, std::string &error);
bool compileSource(const std::string &source, const std::string &fileName, IrModule &module,
                   std::string &error);

}

#endif
