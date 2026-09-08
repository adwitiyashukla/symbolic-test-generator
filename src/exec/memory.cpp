#include "exec/memory.h"

#include "util/support.h"

namespace symx {

uint64_t Memory::create(uint64_t size, const std::string &name, bool isInput, bool isLocal) {
  MemoryObjectPtr object = std::make_shared<MemoryObject>();
  object->id = objects_.size() + 1;
  object->size = size;
  object->name = name;
  object->isInput = isInput;
  object->isLocal = isLocal;
  object->bytes.assign(size, bv::constant(8, 0));
  objects_.push_back(object);
  return object->id;
}

bool Memory::valid(uint64_t objectId) const { return objectId >= 1 && objectId <= objects_.size(); }

const MemoryObject &Memory::object(uint64_t objectId) const { return *objects_[objectId - 1]; }

MemoryObject &Memory::mutableObject(uint64_t objectId) {
  MemoryObjectPtr &slot = objects_[objectId - 1];
  if (slot.use_count() > 1) {
    slot = std::make_shared<MemoryObject>(*slot);
  }
  return *slot;
}

void Memory::fillWithSymbols(uint64_t objectId, unsigned firstSymbolId) {
  MemoryObject &target = mutableObject(objectId);
  for (uint64_t i = 0; i < target.size; i++) {
    target.bytes[i] = bv::symbol(firstSymbolId + static_cast<unsigned>(i));
  }
}

void Memory::fillWithBytes(uint64_t objectId, const std::vector<uint8_t> &values) {
  MemoryObject &target = mutableObject(objectId);
  for (uint64_t i = 0; i < target.size; i++) {
    uint8_t value = i < values.size() ? values[i] : 0;
    target.bytes[i] = bv::constant(8, value);
  }
}

void Memory::fillWithZeroes(uint64_t objectId) {
  MemoryObject &target = mutableObject(objectId);
  for (uint64_t i = 0; i < target.size; i++) {
    target.bytes[i] = bv::constant(8, 0);
  }
}

ExprRef Memory::readConcrete(uint64_t objectId, uint64_t offset, unsigned bytes) const {
  const MemoryObject &source = object(objectId);
  ExprRef result = source.bytes[offset];
  for (unsigned i = 1; i < bytes; i++) {
    result = bv::concat(source.bytes[offset + i], result);
  }
  return result;
}

void Memory::writeConcrete(uint64_t objectId, uint64_t offset, ExprRef value, unsigned bytes) {
  MemoryObject &target = mutableObject(objectId);
  for (unsigned i = 0; i < bytes; i++) {
    target.bytes[offset + i] = bv::extract(value, 8 * i, 8);
  }
}

ExprRef Memory::readSymbolic(uint64_t objectId, ExprRef offset, unsigned bytes) const {
  const MemoryObject &source = object(objectId);
  uint64_t limit = source.size >= bytes ? source.size - bytes : 0;
  ExprRef result = readConcrete(objectId, 0, bytes);
  for (uint64_t position = 1; position <= limit; position++) {
    ExprRef candidate = readConcrete(objectId, position, bytes);
    ExprRef matches = bv::eq(offset, bv::constant(offset->width, position));
    result = bv::ite(matches, candidate, result);
  }
  return result;
}

void Memory::writeSymbolic(uint64_t objectId, ExprRef offset, ExprRef value, unsigned bytes) {
  MemoryObject &target = mutableObject(objectId);
  for (unsigned part = 0; part < bytes; part++) {
    ExprRef partValue = bv::extract(value, 8 * part, 8);
    ExprRef partOffset = bv::add(offset, bv::constant(offset->width, part));
    for (uint64_t position = 0; position < target.size; position++) {
      ExprRef matches = bv::eq(partOffset, bv::constant(offset->width, position));
      target.bytes[position] = bv::ite(matches, partValue, target.bytes[position]);
    }
  }
}

}
