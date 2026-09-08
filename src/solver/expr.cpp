#include "solver/expr.h"

#include <deque>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

#include "util/support.h"

namespace symx {
namespace {

uint64_t mixHash(uint64_t h, uint64_t v) {
  h ^= v + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
  return h;
}

struct NodeHash {
  size_t operator()(ExprRef e) const { return static_cast<size_t>(e->hash); }
};

struct NodeEqual {
  bool operator()(ExprRef x, ExprRef y) const {
    return x->kind == y->kind && x->width == y->width && x->value == y->value && x->a == y->a &&
           x->b == y->b && x->c == y->c;
  }
};

struct Pool {
  std::deque<Expr> nodes;
  std::unordered_set<ExprRef, NodeHash, NodeEqual> table;
  std::vector<std::string> arrayNames;
  std::vector<unsigned> byteIndices;
};

Pool &pool() {
  static Pool instance;
  return instance;
}

uint64_t computeHash(const Expr &node) {
  uint64_t h = 1469598103934665603ULL;
  h = mixHash(h, static_cast<uint64_t>(node.kind));
  h = mixHash(h, node.width);
  h = mixHash(h, node.value);
  h = mixHash(h, reinterpret_cast<uintptr_t>(node.a));
  h = mixHash(h, reinterpret_cast<uintptr_t>(node.b));
  h = mixHash(h, reinterpret_cast<uintptr_t>(node.c));
  return h;
}

ExprRef intern(ExprKind kind, unsigned width, uint64_t value, ExprRef a, ExprRef b, ExprRef c) {
  Expr probe;
  probe.kind = kind;
  probe.width = static_cast<uint16_t>(width);
  probe.value = value;
  probe.a = a;
  probe.b = b;
  probe.c = c;
  probe.hash = computeHash(probe);
  Pool &p = pool();
  auto it = p.table.find(&probe);
  if (it != p.table.end()) {
    return *it;
  }
  p.nodes.push_back(probe);
  ExprRef ref = &p.nodes.back();
  p.table.insert(ref);
  return ref;
}

bool isPowerOfTwo(uint64_t v) { return v != 0 && (v & (v - 1)) == 0; }

unsigned log2Exact(uint64_t v) {
  unsigned n = 0;
  while ((v >> n) != 1) {
    n++;
  }
  return n;
}

}

const char *exprKindName(ExprKind kind) {
  switch (kind) {
    case ExprKind::Constant:
      return "const";
    case ExprKind::Symbol:
      return "sym";
    case ExprKind::Concat:
      return "concat";
    case ExprKind::Extract:
      return "extract";
    case ExprKind::ZExt:
      return "zext";
    case ExprKind::SExt:
      return "sext";
    case ExprKind::Add:
      return "add";
    case ExprKind::Sub:
      return "sub";
    case ExprKind::Mul:
      return "mul";
    case ExprKind::UDiv:
      return "udiv";
    case ExprKind::SDiv:
      return "sdiv";
    case ExprKind::URem:
      return "urem";
    case ExprKind::SRem:
      return "srem";
    case ExprKind::And:
      return "and";
    case ExprKind::Or:
      return "or";
    case ExprKind::Xor:
      return "xor";
    case ExprKind::Shl:
      return "shl";
    case ExprKind::LShr:
      return "lshr";
    case ExprKind::AShr:
      return "ashr";
    case ExprKind::Not:
      return "not";
    case ExprKind::Eq:
      return "eq";
    case ExprKind::Ult:
      return "ult";
    case ExprKind::Ule:
      return "ule";
    case ExprKind::Slt:
      return "slt";
    case ExprKind::Sle:
      return "sle";
    case ExprKind::Ite:
      return "ite";
  }
  return "?";
}

uint64_t maskFor(unsigned width) {
  if (width >= 64) {
    return ~0ULL;
  }
  return (1ULL << width) - 1ULL;
}

uint64_t truncateTo(uint64_t value, unsigned width) { return value & maskFor(width); }

uint64_t signExtendFrom(uint64_t value, unsigned width) {
  if (width >= 64) {
    return value;
  }
  uint64_t signBit = 1ULL << (width - 1);
  if (value & signBit) {
    return value | ~maskFor(width);
  }
  return value & maskFor(width);
}

int64_t asSigned(uint64_t value, unsigned width) {
  return static_cast<int64_t>(signExtendFrom(value, width));
}

bool isConstant(ExprRef e) { return e->kind == ExprKind::Constant; }

uint64_t constantValue(ExprRef e) { return e->value; }

bool isTrue(ExprRef e) { return e->kind == ExprKind::Constant && e->width == 1 && e->value == 1; }

bool isFalse(ExprRef e) { return e->kind == ExprKind::Constant && e->width == 1 && e->value == 0; }

unsigned addSymbolByte(const std::string &name, unsigned index) {
  Pool &p = pool();
  p.arrayNames.push_back(name);
  p.byteIndices.push_back(index);
  return static_cast<unsigned>(p.arrayNames.size() - 1);
}

const std::string &symbolArrayName(unsigned symbolId) { return pool().arrayNames[symbolId]; }

unsigned symbolByteIndex(unsigned symbolId) { return pool().byteIndices[symbolId]; }

unsigned symbolCount() { return static_cast<unsigned>(pool().arrayNames.size()); }

void collectSymbols(ExprRef e, std::vector<unsigned> &out) {
  std::unordered_set<ExprRef> seen;
  std::vector<ExprRef> stack{e};
  while (!stack.empty()) {
    ExprRef cur = stack.back();
    stack.pop_back();
    if (!cur || !seen.insert(cur).second) {
      continue;
    }
    if (cur->kind == ExprKind::Symbol) {
      out.push_back(static_cast<unsigned>(cur->value));
      continue;
    }
    stack.push_back(cur->a);
    stack.push_back(cur->b);
    stack.push_back(cur->c);
  }
}

size_t exprNodeCount(ExprRef e) {
  std::unordered_set<ExprRef> seen;
  std::vector<ExprRef> stack{e};
  size_t count = 0;
  while (!stack.empty()) {
    ExprRef cur = stack.back();
    stack.pop_back();
    if (!cur || !seen.insert(cur).second) {
      continue;
    }
    count++;
    stack.push_back(cur->a);
    stack.push_back(cur->b);
    stack.push_back(cur->c);
  }
  return count;
}

std::string exprToString(ExprRef e) {
  if (!e) {
    return "null";
  }
  std::ostringstream os;
  if (e->kind == ExprKind::Constant) {
    os << e->value << ":" << e->width;
    return os.str();
  }
  if (e->kind == ExprKind::Symbol) {
    os << symbolArrayName(static_cast<unsigned>(e->value)) << "["
       << symbolByteIndex(static_cast<unsigned>(e->value)) << "]";
    return os.str();
  }
  os << "(" << exprKindName(e->kind);
  if (e->kind == ExprKind::Extract) {
    os << " " << e->value << " " << e->width;
  } else if (e->kind == ExprKind::ZExt || e->kind == ExprKind::SExt) {
    os << " " << e->width;
  }
  if (e->a) {
    os << " " << exprToString(e->a);
  }
  if (e->b) {
    os << " " << exprToString(e->b);
  }
  if (e->c) {
    os << " " << exprToString(e->c);
  }
  os << ")";
  return os.str();
}

namespace {

uint64_t evalNode(ExprRef e, const std::vector<uint8_t> &assignment,
                  std::unordered_map<ExprRef, uint64_t> &memo);

uint64_t evalChild(ExprRef e, const std::vector<uint8_t> &assignment,
                   std::unordered_map<ExprRef, uint64_t> &memo) {
  return evalNode(e, assignment, memo);
}

uint64_t evalNode(ExprRef e, const std::vector<uint8_t> &assignment,
                  std::unordered_map<ExprRef, uint64_t> &memo) {
  auto found = memo.find(e);
  if (found != memo.end()) {
    return found->second;
  }
  uint64_t result = 0;
  unsigned width = e->width;
  switch (e->kind) {
    case ExprKind::Constant:
      result = e->value;
      break;
    case ExprKind::Symbol: {
      unsigned id = static_cast<unsigned>(e->value);
      result = id < assignment.size() ? assignment[id] : 0;
      break;
    }
    case ExprKind::Concat: {
      uint64_t hi = evalChild(e->a, assignment, memo);
      uint64_t lo = evalChild(e->b, assignment, memo);
      result = truncateTo((hi << e->b->width) | lo, width);
      break;
    }
    case ExprKind::Extract: {
      uint64_t v = evalChild(e->a, assignment, memo);
      result = truncateTo(v >> e->value, width);
      break;
    }
    case ExprKind::ZExt:
      result = truncateTo(evalChild(e->a, assignment, memo), e->a->width);
      break;
    case ExprKind::SExt:
      result = truncateTo(signExtendFrom(evalChild(e->a, assignment, memo), e->a->width), width);
      break;
    case ExprKind::Add:
      result =
          truncateTo(evalChild(e->a, assignment, memo) + evalChild(e->b, assignment, memo), width);
      break;
    case ExprKind::Sub:
      result =
          truncateTo(evalChild(e->a, assignment, memo) - evalChild(e->b, assignment, memo), width);
      break;
    case ExprKind::Mul:
      result =
          truncateTo(evalChild(e->a, assignment, memo) * evalChild(e->b, assignment, memo), width);
      break;
    case ExprKind::UDiv: {
      uint64_t x = evalChild(e->a, assignment, memo);
      uint64_t y = evalChild(e->b, assignment, memo);
      result = y == 0 ? maskFor(width) : truncateTo(x / y, width);
      break;
    }
    case ExprKind::URem: {
      uint64_t x = evalChild(e->a, assignment, memo);
      uint64_t y = evalChild(e->b, assignment, memo);
      result = y == 0 ? x : truncateTo(x % y, width);
      break;
    }
    case ExprKind::SDiv: {
      int64_t x = asSigned(evalChild(e->a, assignment, memo), width);
      int64_t y = asSigned(evalChild(e->b, assignment, memo), width);
      if (y == 0) {
        result = x >= 0 ? maskFor(width) : 1;
      } else if (y == -1) {
        result = truncateTo(static_cast<uint64_t>(0) - static_cast<uint64_t>(x), width);
      } else {
        result = truncateTo(static_cast<uint64_t>(x / y), width);
      }
      break;
    }
    case ExprKind::SRem: {
      int64_t x = asSigned(evalChild(e->a, assignment, memo), width);
      int64_t y = asSigned(evalChild(e->b, assignment, memo), width);
      if (y == 0 || y == -1) {
        result = y == 0 ? truncateTo(static_cast<uint64_t>(x), width) : 0;
      } else {
        result = truncateTo(static_cast<uint64_t>(x % y), width);
      }
      break;
    }
    case ExprKind::And:
      result = evalChild(e->a, assignment, memo) & evalChild(e->b, assignment, memo);
      break;
    case ExprKind::Or:
      result = evalChild(e->a, assignment, memo) | evalChild(e->b, assignment, memo);
      break;
    case ExprKind::Xor:
      result = evalChild(e->a, assignment, memo) ^ evalChild(e->b, assignment, memo);
      break;
    case ExprKind::Shl: {
      uint64_t x = evalChild(e->a, assignment, memo);
      uint64_t s = evalChild(e->b, assignment, memo);
      result = s >= width ? 0 : truncateTo(x << s, width);
      break;
    }
    case ExprKind::LShr: {
      uint64_t x = evalChild(e->a, assignment, memo);
      uint64_t s = evalChild(e->b, assignment, memo);
      result = s >= width ? 0 : truncateTo(x >> s, width);
      break;
    }
    case ExprKind::AShr: {
      int64_t x = asSigned(evalChild(e->a, assignment, memo), width);
      uint64_t s = evalChild(e->b, assignment, memo);
      if (s >= width) {
        result = x < 0 ? maskFor(width) : 0;
      } else {
        result = truncateTo(static_cast<uint64_t>(x >> s), width);
      }
      break;
    }
    case ExprKind::Not:
      result = evalChild(e->a, assignment, memo) ^ 1ULL;
      break;
    case ExprKind::Eq:
      result = evalChild(e->a, assignment, memo) == evalChild(e->b, assignment, memo) ? 1 : 0;
      break;
    case ExprKind::Ult:
      result = evalChild(e->a, assignment, memo) < evalChild(e->b, assignment, memo) ? 1 : 0;
      break;
    case ExprKind::Ule:
      result = evalChild(e->a, assignment, memo) <= evalChild(e->b, assignment, memo) ? 1 : 0;
      break;
    case ExprKind::Slt:
      result = asSigned(evalChild(e->a, assignment, memo), e->a->width) <
                       asSigned(evalChild(e->b, assignment, memo), e->b->width)
                   ? 1
                   : 0;
      break;
    case ExprKind::Sle:
      result = asSigned(evalChild(e->a, assignment, memo), e->a->width) <=
                       asSigned(evalChild(e->b, assignment, memo), e->b->width)
                   ? 1
                   : 0;
      break;
    case ExprKind::Ite:
      result = evalChild(e->a, assignment, memo) != 0 ? evalChild(e->b, assignment, memo)
                                                      : evalChild(e->c, assignment, memo);
      break;
  }
  memo.emplace(e, result);
  return result;
}

}

uint64_t evaluateExpr(ExprRef e, const std::vector<uint8_t> &assignment) {
  std::unordered_map<ExprRef, uint64_t> memo;
  return evalNode(e, assignment, memo);
}

namespace bv {

ExprRef constant(unsigned width, uint64_t value) {
  if (width == 0 || width > 64) {
    fatal("constant width out of range");
  }
  return intern(ExprKind::Constant, width, truncateTo(value, width), nullptr, nullptr, nullptr);
}

ExprRef symbol(unsigned symbolId) {
  return intern(ExprKind::Symbol, 8, symbolId, nullptr, nullptr, nullptr);
}

ExprRef boolConst(bool value) { return constant(1, value ? 1 : 0); }

ExprRef truth() { return boolConst(true); }

ExprRef falsity() { return boolConst(false); }

ExprRef concat(ExprRef high, ExprRef low) {
  unsigned width = high->width + low->width;
  if (width > 64) {
    fatal("concat wider than 64 bits");
  }
  if (isConstant(high) && isConstant(low)) {
    return constant(width, (high->value << low->width) | low->value);
  }
  if (high->kind == ExprKind::Extract && low->kind == ExprKind::Extract && high->a == low->a &&
      high->value == low->value + low->width) {
    return extract(high->a, static_cast<unsigned>(low->value), width);
  }
  return intern(ExprKind::Concat, width, 0, high, low, nullptr);
}

ExprRef extract(ExprRef a, unsigned offset, unsigned width) {
  if (width == 0 || offset + width > a->width) {
    fatal("extract out of range");
  }
  if (offset == 0 && width == a->width) {
    return a;
  }
  if (isConstant(a)) {
    return constant(width, a->value >> offset);
  }
  if (a->kind == ExprKind::Extract) {
    return extract(a->a, offset + static_cast<unsigned>(a->value), width);
  }
  if (a->kind == ExprKind::Concat) {
    unsigned lowWidth = a->b->width;
    if (offset + width <= lowWidth) {
      return extract(a->b, offset, width);
    }
    if (offset >= lowWidth) {
      return extract(a->a, offset - lowWidth, width);
    }
  }
  if (a->kind == ExprKind::ZExt) {
    unsigned sourceWidth = a->a->width;
    if (offset + width <= sourceWidth) {
      return extract(a->a, offset, width);
    }
    if (offset >= sourceWidth) {
      return constant(width, 0);
    }
  }
  if (a->kind == ExprKind::SExt && offset + width <= a->a->width) {
    return extract(a->a, offset, width);
  }
  if (a->kind == ExprKind::And) {
    return bitAnd(extract(a->a, offset, width), extract(a->b, offset, width));
  }
  if (a->kind == ExprKind::Or) {
    return bitOr(extract(a->a, offset, width), extract(a->b, offset, width));
  }
  if (a->kind == ExprKind::Xor) {
    return bitXor(extract(a->a, offset, width), extract(a->b, offset, width));
  }
  return intern(ExprKind::Extract, width, offset, a, nullptr, nullptr);
}

ExprRef zext(ExprRef a, unsigned width) {
  if (width < a->width) {
    fatal("zext to smaller width");
  }
  if (width == a->width) {
    return a;
  }
  if (isConstant(a)) {
    return constant(width, a->value);
  }
  if (a->kind == ExprKind::ZExt) {
    return zext(a->a, width);
  }
  return intern(ExprKind::ZExt, width, 0, a, nullptr, nullptr);
}

ExprRef sext(ExprRef a, unsigned width) {
  if (width < a->width) {
    fatal("sext to smaller width");
  }
  if (width == a->width) {
    return a;
  }
  if (isConstant(a)) {
    return constant(width, signExtendFrom(a->value, a->width));
  }
  return intern(ExprKind::SExt, width, 0, a, nullptr, nullptr);
}

ExprRef resize(ExprRef a, unsigned width, bool isSigned) {
  if (width == a->width) {
    return a;
  }
  if (width < a->width) {
    return extract(a, 0, width);
  }
  return isSigned ? sext(a, width) : zext(a, width);
}

ExprRef add(ExprRef a, ExprRef b) {
  unsigned width = a->width;
  if (isConstant(a) && isConstant(b)) {
    return constant(width, a->value + b->value);
  }
  if (isConstant(a)) {
    ExprRef swap = a;
    a = b;
    b = swap;
  }
  if (isConstant(b)) {
    if (b->value == 0) {
      return a;
    }
    if (a->kind == ExprKind::Add && isConstant(a->b)) {
      return add(a->a, constant(width, a->b->value + b->value));
    }
  }
  return intern(ExprKind::Add, width, 0, a, b, nullptr);
}

ExprRef sub(ExprRef a, ExprRef b) {
  unsigned width = a->width;
  if (a == b) {
    return constant(width, 0);
  }
  if (isConstant(a) && isConstant(b)) {
    return constant(width, a->value - b->value);
  }
  if (isConstant(b)) {
    return add(a, constant(width, 0ULL - b->value));
  }
  return intern(ExprKind::Sub, width, 0, a, b, nullptr);
}

ExprRef neg(ExprRef a) { return sub(constant(a->width, 0), a); }

ExprRef mul(ExprRef a, ExprRef b) {
  unsigned width = a->width;
  if (isConstant(a) && isConstant(b)) {
    return constant(width, a->value * b->value);
  }
  if (isConstant(a)) {
    ExprRef swap = a;
    a = b;
    b = swap;
  }
  if (isConstant(b)) {
    if (b->value == 0) {
      return constant(width, 0);
    }
    if (b->value == 1) {
      return a;
    }
    if (isPowerOfTwo(b->value)) {
      return shl(a, constant(width, log2Exact(b->value)));
    }
    unsigned setBits = 0;
    for (unsigned i = 0; i < width; i++) {
      if ((b->value >> i) & 1ULL) {
        setBits++;
      }
    }
    if (setBits <= 3) {
      ExprRef sum = nullptr;
      for (unsigned i = 0; i < width; i++) {
        if (((b->value >> i) & 1ULL) == 0) {
          continue;
        }
        ExprRef term = i == 0 ? a : shl(a, constant(width, i));
        sum = sum == nullptr ? term : add(sum, term);
      }
      return sum;
    }
  }
  return intern(ExprKind::Mul, width, 0, a, b, nullptr);
}

ExprRef udiv(ExprRef a, ExprRef b) {
  unsigned width = a->width;
  if (isConstant(b)) {
    if (b->value == 0) {
      return constant(width, maskFor(width));
    }
    if (b->value == 1) {
      return a;
    }
    if (isConstant(a)) {
      return constant(width, a->value / b->value);
    }
    if (isPowerOfTwo(b->value)) {
      return lshr(a, constant(width, log2Exact(b->value)));
    }
  }
  return intern(ExprKind::UDiv, width, 0, a, b, nullptr);
}

ExprRef urem(ExprRef a, ExprRef b) {
  unsigned width = a->width;
  if (isConstant(b)) {
    if (b->value == 0) {
      return a;
    }
    if (b->value == 1) {
      return constant(width, 0);
    }
    if (isConstant(a)) {
      return constant(width, a->value % b->value);
    }
    if (isPowerOfTwo(b->value)) {
      return bitAnd(a, constant(width, b->value - 1));
    }
  }
  return intern(ExprKind::URem, width, 0, a, b, nullptr);
}

ExprRef sdiv(ExprRef a, ExprRef b) {
  unsigned width = a->width;
  if (isConstant(b) && b->value == 1) {
    return a;
  }
  if (isConstant(a) && isConstant(b)) {
    std::vector<uint8_t> empty;
    return constant(width, evaluateExpr(intern(ExprKind::SDiv, width, 0, a, b, nullptr), empty));
  }
  return intern(ExprKind::SDiv, width, 0, a, b, nullptr);
}

ExprRef srem(ExprRef a, ExprRef b) {
  unsigned width = a->width;
  if (isConstant(b) && b->value == 1) {
    return constant(width, 0);
  }
  if (isConstant(a) && isConstant(b)) {
    std::vector<uint8_t> empty;
    return constant(width, evaluateExpr(intern(ExprKind::SRem, width, 0, a, b, nullptr), empty));
  }
  return intern(ExprKind::SRem, width, 0, a, b, nullptr);
}

ExprRef bitAnd(ExprRef a, ExprRef b) {
  unsigned width = a->width;
  if (isConstant(a) && isConstant(b)) {
    return constant(width, a->value & b->value);
  }
  if (a == b) {
    return a;
  }
  if (isConstant(a)) {
    ExprRef swap = a;
    a = b;
    b = swap;
  }
  if (isConstant(b)) {
    if (b->value == 0) {
      return constant(width, 0);
    }
    if (b->value == maskFor(width)) {
      return a;
    }
  }
  return intern(ExprKind::And, width, 0, a, b, nullptr);
}

ExprRef bitOr(ExprRef a, ExprRef b) {
  unsigned width = a->width;
  if (isConstant(a) && isConstant(b)) {
    return constant(width, a->value | b->value);
  }
  if (a == b) {
    return a;
  }
  if (isConstant(a)) {
    ExprRef swap = a;
    a = b;
    b = swap;
  }
  if (isConstant(b)) {
    if (b->value == 0) {
      return a;
    }
    if (b->value == maskFor(width)) {
      return constant(width, maskFor(width));
    }
  }
  return intern(ExprKind::Or, width, 0, a, b, nullptr);
}

ExprRef bitXor(ExprRef a, ExprRef b) {
  unsigned width = a->width;
  if (isConstant(a) && isConstant(b)) {
    return constant(width, a->value ^ b->value);
  }
  if (a == b) {
    return constant(width, 0);
  }
  if (isConstant(a)) {
    ExprRef swap = a;
    a = b;
    b = swap;
  }
  if (isConstant(b) && b->value == 0) {
    return a;
  }
  return intern(ExprKind::Xor, width, 0, a, b, nullptr);
}

ExprRef bitNot(ExprRef a) {
  if (a->width == 1) {
    return logicalNot(a);
  }
  return bitXor(a, constant(a->width, maskFor(a->width)));
}

ExprRef shl(ExprRef a, ExprRef b) {
  unsigned width = a->width;
  if (isConstant(b)) {
    if (b->value == 0) {
      return a;
    }
    if (b->value >= width) {
      return constant(width, 0);
    }
    if (isConstant(a)) {
      return constant(width, a->value << b->value);
    }
  }
  return intern(ExprKind::Shl, width, 0, a, b, nullptr);
}

ExprRef lshr(ExprRef a, ExprRef b) {
  unsigned width = a->width;
  if (isConstant(b)) {
    if (b->value == 0) {
      return a;
    }
    if (b->value >= width) {
      return constant(width, 0);
    }
    if (isConstant(a)) {
      return constant(width, a->value >> b->value);
    }
    unsigned shift = static_cast<unsigned>(b->value);
    return zext(extract(a, shift, width - shift), width);
  }
  return intern(ExprKind::LShr, width, 0, a, b, nullptr);
}

ExprRef ashr(ExprRef a, ExprRef b) {
  unsigned width = a->width;
  if (isConstant(b)) {
    if (b->value == 0) {
      return a;
    }
    if (isConstant(a)) {
      uint64_t shift = b->value >= width ? width - 1 : b->value;
      return constant(width, static_cast<uint64_t>(asSigned(a->value, width) >> shift));
    }
    unsigned shift = b->value >= width ? width - 1 : static_cast<unsigned>(b->value);
    return sext(extract(a, shift, width - shift), width);
  }
  return intern(ExprKind::AShr, width, 0, a, b, nullptr);
}

ExprRef logicalNot(ExprRef a) {
  if (isConstant(a)) {
    return boolConst(a->value == 0);
  }
  if (a->kind == ExprKind::Not) {
    return a->a;
  }
  return intern(ExprKind::Not, 1, 0, a, nullptr, nullptr);
}

ExprRef logicalAnd(ExprRef a, ExprRef b) { return bitAnd(a, b); }

ExprRef logicalOr(ExprRef a, ExprRef b) { return bitOr(a, b); }

ExprRef eq(ExprRef a, ExprRef b) {
  if (a == b) {
    return truth();
  }
  if (isConstant(a) && isConstant(b)) {
    return boolConst(a->value == b->value);
  }
  if (isConstant(a)) {
    ExprRef swap = a;
    a = b;
    b = swap;
  }
  if (isConstant(b)) {
    if (a->width == 1) {
      return b->value == 1 ? a : logicalNot(a);
    }
    if (a->kind == ExprKind::Add && isConstant(a->b)) {
      return eq(a->a, constant(a->width, b->value - a->b->value));
    }
    if (a->kind == ExprKind::Concat) {
      unsigned lowWidth = a->b->width;
      ExprRef highPart = constant(a->a->width, b->value >> lowWidth);
      ExprRef lowPart = constant(lowWidth, b->value);
      return logicalAnd(eq(a->a, highPart), eq(a->b, lowPart));
    }
    if (a->kind == ExprKind::ZExt) {
      unsigned sourceWidth = a->a->width;
      if ((b->value >> sourceWidth) != 0) {
        return falsity();
      }
      return eq(a->a, constant(sourceWidth, b->value));
    }
  }
  return intern(ExprKind::Eq, 1, 0, a, b, nullptr);
}

ExprRef ne(ExprRef a, ExprRef b) { return logicalNot(eq(a, b)); }

ExprRef ult(ExprRef a, ExprRef b) {
  unsigned width = a->width;
  if (a == b) {
    return falsity();
  }
  if (isConstant(a) && isConstant(b)) {
    return boolConst(a->value < b->value);
  }
  if (isConstant(b) && b->value == 0) {
    return falsity();
  }
  if (isConstant(a) && a->value == maskFor(width)) {
    return falsity();
  }
  if (isConstant(a) && a->value == 0) {
    return ne(b, constant(width, 0));
  }
  return intern(ExprKind::Ult, 1, 0, a, b, nullptr);
}

ExprRef ule(ExprRef a, ExprRef b) {
  unsigned width = a->width;
  if (a == b) {
    return truth();
  }
  if (isConstant(a) && isConstant(b)) {
    return boolConst(a->value <= b->value);
  }
  if (isConstant(a) && a->value == 0) {
    return truth();
  }
  if (isConstant(b) && b->value == maskFor(width)) {
    return truth();
  }
  if (isConstant(b) && b->value == 0) {
    return eq(a, constant(width, 0));
  }
  return intern(ExprKind::Ule, 1, 0, a, b, nullptr);
}

ExprRef ugt(ExprRef a, ExprRef b) { return ult(b, a); }

ExprRef uge(ExprRef a, ExprRef b) { return ule(b, a); }

ExprRef slt(ExprRef a, ExprRef b) {
  unsigned width = a->width;
  if (a == b) {
    return falsity();
  }
  if (isConstant(a) && isConstant(b)) {
    return boolConst(asSigned(a->value, width) < asSigned(b->value, width));
  }
  return intern(ExprKind::Slt, 1, 0, a, b, nullptr);
}

ExprRef sle(ExprRef a, ExprRef b) {
  unsigned width = a->width;
  if (a == b) {
    return truth();
  }
  if (isConstant(a) && isConstant(b)) {
    return boolConst(asSigned(a->value, width) <= asSigned(b->value, width));
  }
  return intern(ExprKind::Sle, 1, 0, a, b, nullptr);
}

ExprRef sgt(ExprRef a, ExprRef b) { return slt(b, a); }

ExprRef sge(ExprRef a, ExprRef b) { return sle(b, a); }

ExprRef ite(ExprRef cond, ExprRef whenTrue, ExprRef whenFalse) {
  if (isConstant(cond)) {
    return cond->value != 0 ? whenTrue : whenFalse;
  }
  if (whenTrue == whenFalse) {
    return whenTrue;
  }
  if (whenTrue->width == 1 && isConstant(whenTrue) && isConstant(whenFalse)) {
    return whenTrue->value == 1 ? cond : logicalNot(cond);
  }
  return intern(ExprKind::Ite, whenTrue->width, 0, cond, whenTrue, whenFalse);
}

ExprRef isZero(ExprRef a) { return eq(a, constant(a->width, 0)); }

ExprRef isNonZero(ExprRef a) { return ne(a, constant(a->width, 0)); }

}

}
