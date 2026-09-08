#ifndef SYMX_UTIL_SUPPORT_H
#define SYMX_UTIL_SUPPORT_H

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

namespace symx {

[[noreturn]] void fatal(const std::string &message);

std::string readFileOrDie(const std::string &path);
bool writeFile(const std::string &path, const std::string &contents);
bool makeDirectory(const std::string &path);
bool fileExists(const std::string &path);
std::string joinPath(const std::string &directory, const std::string &name);

std::vector<std::string> splitLines(const std::string &text);
std::string trim(const std::string &text);
bool startsWith(const std::string &text, const std::string &prefix);
bool endsWith(const std::string &text, const std::string &suffix);
std::string jsonEscape(const std::string &text);

class Rng {
 public:
  explicit Rng(uint64_t seed) : state_(seed ? seed : 0x9e3779b97f4a7c15ULL) {}

  uint64_t next() {
    state_ ^= state_ << 13;
    state_ ^= state_ >> 7;
    state_ ^= state_ << 17;
    return state_;
  }

  uint32_t below(uint32_t bound) { return bound == 0 ? 0 : static_cast<uint32_t>(next() % bound); }

  double unit() { return static_cast<double>(next() >> 11) / 9007199254740992.0; }

 private:
  uint64_t state_;
};

class Timer {
 public:
  Timer() : start_(std::chrono::steady_clock::now()) {}

  void reset() { start_ = std::chrono::steady_clock::now(); }

  double seconds() const {
    auto now = std::chrono::steady_clock::now();
    return std::chrono::duration<double>(now - start_).count();
  }

 private:
  std::chrono::steady_clock::time_point start_;
};

}

#endif
