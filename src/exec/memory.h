#ifndef SYMX_EXEC_MEMORY_H
#define SYMX_EXEC_MEMORY_H

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "solver/expr.h"

namespace symx {

constexpr uint64_t kObjectShift = 32;
constexpr uint64_t kOffsetMask = (1ULL << kObjectShift) - 1ULL;

inline uint64_t addressOfObject(uint64_t objectId) { return objectId << kObjectShift; }
inline uint64_t objectIdOfAddress(uint64_t address) { return address >> kObjectShift; }
inline uint64_t offsetOfAddress(uint64_t address) { return address & kOffsetMask; }

struct MemoryObject {
  uint64_t id = 0;
  uint64_t size = 0;
  std::string name;
  bool isInput = false;
  bool isLocal = false;
  std::vector<ExprRef> bytes;
};

using MemoryObjectPtr = std::shared_ptr<MemoryObject>;

class Memory {
 public:
  uint64_t create(uint64_t size, const std::string &name, bool isInput, bool isLocal);
  bool valid(uint64_t objectId) const;
  const MemoryObject &object(uint64_t objectId) const;
  MemoryObject &mutableObject(uint64_t objectId);
  size_t objectCount() const { return objects_.size(); }
  void fillWithSymbols(uint64_t objectId, unsigned firstSymbolId);
  void fillWithBytes(uint64_t objectId, const std::vector<uint8_t> &values);
  void fillWithZeroes(uint64_t objectId);

  ExprRef readConcrete(uint64_t objectId, uint64_t offset, unsigned bytes) const;
  void writeConcrete(uint64_t objectId, uint64_t offset, ExprRef value, unsigned bytes);
  ExprRef readSymbolic(uint64_t objectId, ExprRef offset, unsigned bytes) const;
  void writeSymbolic(uint64_t objectId, ExprRef offset, ExprRef value, unsigned bytes);

 private:
  std::vector<MemoryObjectPtr> objects_;
};

}

#endif
