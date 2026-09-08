#ifndef SYMX_UTIL_CONFIG_H
#define SYMX_UTIL_CONFIG_H

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace symx {

enum class ConfigValueKind { Integer, Number, Boolean, Text };

struct ConfigValue {
  ConfigValueKind kind = ConfigValueKind::Integer;
  int64_t integer = 0;
  double number = 0.0;
  bool boolean = false;
  std::string text;
  int line = 0;
};

class ConfigFile {
 public:
  bool parse(const std::string &contents, std::string &error);
  bool has(const std::string &key) const;
  const ConfigValue *find(const std::string &key) const;
  const std::vector<std::string> &keysInOrder() const { return order_; }

 private:
  std::unordered_map<std::string, ConfigValue> values_;
  std::vector<std::string> order_;
};

struct Settings {
  std::string strategy = "coverage";
  int64_t maxStates = 4000;
  int64_t maxSteps = 2000000;
  int64_t maxForksPerPath = 2000;
  int64_t maxCallDepth = 64;
  int64_t maxTests = 512;
  int64_t bufferElements = 8;
  int64_t symbolicOffsetLimit = 512;
  int64_t seed = 20260908;
  int64_t solverConflictBudget = -1;
  int64_t randomCount = 5000;
  double maxSeconds = 60.0;
  bool checkShiftAmount = false;
  bool solverIndependence = true;
  bool solverCache = true;
};

bool loadSettings(const std::string &path, Settings &settings, std::string &error);
bool applySettings(const ConfigFile &file, Settings &settings, std::string &error);

}

#endif
