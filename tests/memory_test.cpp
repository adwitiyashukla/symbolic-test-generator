#include "exec/memory.h"

#include <vector>

#include "solver/expr.h"
#include "tests/test_util.h"

namespace {

using symx::ExprRef;
using symx::Memory;

SYMX_TEST(ConcreteReadsAndWritesRoundTrip) {
  Memory memory;
  uint64_t object = memory.create(8, "buffer", false, true);
  memory.writeConcrete(object, 0, symx::bv::constant(32, 0x11223344), 4);
  ExprRef value = memory.readConcrete(object, 0, 4);
  CHECK_EQ(symx::constantValue(value), 0x11223344ULL);
  CHECK_EQ(symx::constantValue(memory.readConcrete(object, 1, 1)), 0x33ULL);
  memory.writeConcrete(object, 4, symx::bv::constant(16, 0xbeef), 2);
  CHECK_EQ(symx::constantValue(memory.readConcrete(object, 4, 2)), 0xbeefULL);
}

SYMX_TEST(AddressesEncodeObjectAndOffset) {
  Memory memory;
  uint64_t first = memory.create(4, "a", false, true);
  uint64_t second = memory.create(4, "b", false, true);
  CHECK(first != second);
  uint64_t address = symx::addressOfObject(second) + 3;
  CHECK_EQ(symx::objectIdOfAddress(address), second);
  CHECK_EQ(symx::offsetOfAddress(address), 3ULL);
}

SYMX_TEST(SymbolicOffsetReadSelectsTheRightByte) {
  Memory memory;
  uint64_t object = memory.create(4, "table", false, true);
  for (uint64_t i = 0; i < 4; i++) {
    memory.writeConcrete(object, i, symx::bv::constant(8, 10 + i), 1);
  }
  unsigned symbol = symx::addSymbolByte("offset", 0);
  ExprRef offset = symx::bv::zext(symx::bv::symbol(symbol), 32);
  ExprRef value = memory.readSymbolic(object, offset, 1);
  std::vector<uint8_t> assignment(symx::symbolCount(), 0);
  for (uint8_t position = 0; position < 4; position++) {
    assignment[symbol] = position;
    CHECK_EQ(symx::evaluateExpr(value, assignment), static_cast<uint64_t>(10 + position));
  }
}

SYMX_TEST(SymbolicOffsetWriteOnlyTouchesOnePlace) {
  Memory memory;
  uint64_t object = memory.create(4, "table", false, true);
  for (uint64_t i = 0; i < 4; i++) {
    memory.writeConcrete(object, i, symx::bv::constant(8, 0), 1);
  }
  unsigned symbol = symx::addSymbolByte("writeoffset", 0);
  ExprRef offset = symx::bv::zext(symx::bv::symbol(symbol), 32);
  memory.writeSymbolic(object, offset, symx::bv::constant(8, 99), 1);
  std::vector<uint8_t> assignment(symx::symbolCount(), 0);
  for (uint8_t position = 0; position < 4; position++) {
    assignment[symbol] = position;
    for (uint64_t i = 0; i < 4; i++) {
      uint64_t expected = i == position ? 99 : 0;
      CHECK_EQ(symx::evaluateExpr(memory.readConcrete(object, i, 1), assignment), expected);
    }
  }
}

SYMX_TEST(CopiesDoNotShareWrites) {
  Memory original;
  uint64_t object = original.create(4, "shared", false, true);
  original.writeConcrete(object, 0, symx::bv::constant(32, 7), 4);
  Memory copy = original;
  copy.writeConcrete(object, 0, symx::bv::constant(32, 9), 4);
  CHECK_EQ(symx::constantValue(original.readConcrete(object, 0, 4)), 7ULL);
  CHECK_EQ(symx::constantValue(copy.readConcrete(object, 0, 4)), 9ULL);
}

SYMX_TEST(SymbolBytesAreDistinct) {
  Memory memory;
  uint64_t object = memory.create(4, "input", true, false);
  unsigned first = symx::addSymbolByte("input", 0);
  for (unsigned i = 1; i < 4; i++) {
    symx::addSymbolByte("input", i);
  }
  memory.fillWithSymbols(object, first);
  ExprRef word = memory.readConcrete(object, 0, 4);
  std::vector<uint8_t> assignment(symx::symbolCount(), 0);
  assignment[first] = 0x44;
  assignment[first + 1] = 0x33;
  assignment[first + 2] = 0x22;
  assignment[first + 3] = 0x11;
  CHECK_EQ(symx::evaluateExpr(word, assignment), 0x11223344ULL);
}

}

SYMX_TEST_MAIN("memory_test")
