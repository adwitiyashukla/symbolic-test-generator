#include "util/config.h"

#include <cstdlib>
#include <sstream>

#include "util/support.h"

namespace symx {
namespace {

bool parseInteger(const std::string &text, int64_t &value) {
  if (text.empty()) {
    return false;
  }
  char *end = nullptr;
  long long parsed = std::strtoll(text.c_str(), &end, 10);
  if (*end != '\0') {
    return false;
  }
  value = parsed;
  return true;
}

bool parseNumber(const std::string &text, double &value) {
  if (text.empty()) {
    return false;
  }
  char *end = nullptr;
  double parsed = std::strtod(text.c_str(), &end);
  if (*end != '\0') {
    return false;
  }
  value = parsed;
  return true;
}

std::string errorAt(int line, const std::string &message) {
  std::ostringstream os;
  os << "config.toml:" << line << ": " << message;
  return os.str();
}

}

bool ConfigFile::parse(const std::string &contents, std::string &error) {
  std::string section;
  int lineNumber = 0;
  for (const std::string &raw : splitLines(contents)) {
    lineNumber++;
    std::string line = trim(raw);
    size_t comment = line.find('#');
    if (comment != std::string::npos) {
      line = trim(line.substr(0, comment));
    }
    if (line.empty()) {
      continue;
    }
    if (line.front() == '[') {
      if (line.back() != ']') {
        error = errorAt(lineNumber, "unterminated section header");
        return false;
      }
      section = trim(line.substr(1, line.size() - 2));
      if (section.empty()) {
        error = errorAt(lineNumber, "empty section name");
        return false;
      }
      continue;
    }
    size_t equals = line.find('=');
    if (equals == std::string::npos) {
      error = errorAt(lineNumber, "expected key = value");
      return false;
    }
    std::string key = trim(line.substr(0, equals));
    std::string rest = trim(line.substr(equals + 1));
    if (key.empty() || rest.empty()) {
      error = errorAt(lineNumber, "expected key = value");
      return false;
    }
    ConfigValue value;
    value.line = lineNumber;
    if (rest.front() == '"') {
      if (rest.size() < 2 || rest.back() != '"') {
        error = errorAt(lineNumber, "unterminated string");
        return false;
      }
      value.kind = ConfigValueKind::Text;
      value.text = rest.substr(1, rest.size() - 2);
    } else if (rest == "true" || rest == "false") {
      value.kind = ConfigValueKind::Boolean;
      value.boolean = rest == "true";
    } else if (parseInteger(rest, value.integer)) {
      value.kind = ConfigValueKind::Integer;
      value.number = static_cast<double>(value.integer);
    } else if (parseNumber(rest, value.number)) {
      value.kind = ConfigValueKind::Number;
    } else {
      error = errorAt(lineNumber, "value is not an integer, number, boolean or string");
      return false;
    }
    std::string fullKey = section.empty() ? key : section + "." + key;
    if (values_.count(fullKey) != 0) {
      error = errorAt(lineNumber, "duplicate key " + fullKey);
      return false;
    }
    values_.emplace(fullKey, value);
    order_.push_back(fullKey);
  }
  return true;
}

bool ConfigFile::has(const std::string &key) const { return values_.count(key) != 0; }

const ConfigValue *ConfigFile::find(const std::string &key) const {
  auto found = values_.find(key);
  return found == values_.end() ? nullptr : &found->second;
}

namespace {

bool readInteger(const ConfigFile &file, const std::string &key, int64_t minimum, int64_t maximum,
                 int64_t &target, std::string &error) {
  const ConfigValue *value = file.find(key);
  if (value == nullptr) {
    return true;
  }
  if (value->kind != ConfigValueKind::Integer) {
    error = errorAt(value->line, key + " must be an integer");
    return false;
  }
  if (value->integer < minimum || value->integer > maximum) {
    std::ostringstream os;
    os << key << " must be between " << minimum << " and " << maximum;
    error = errorAt(value->line, os.str());
    return false;
  }
  target = value->integer;
  return true;
}

bool readNumber(const ConfigFile &file, const std::string &key, double minimum, double maximum,
                double &target, std::string &error) {
  const ConfigValue *value = file.find(key);
  if (value == nullptr) {
    return true;
  }
  if (value->kind != ConfigValueKind::Integer && value->kind != ConfigValueKind::Number) {
    error = errorAt(value->line, key + " must be a number");
    return false;
  }
  double candidate =
      value->kind == ConfigValueKind::Integer ? static_cast<double>(value->integer) : value->number;
  if (candidate < minimum || candidate > maximum) {
    std::ostringstream os;
    os << key << " must be between " << minimum << " and " << maximum;
    error = errorAt(value->line, os.str());
    return false;
  }
  target = candidate;
  return true;
}

bool readBoolean(const ConfigFile &file, const std::string &key, bool &target, std::string &error) {
  const ConfigValue *value = file.find(key);
  if (value == nullptr) {
    return true;
  }
  if (value->kind != ConfigValueKind::Boolean) {
    error = errorAt(value->line, key + " must be true or false");
    return false;
  }
  target = value->boolean;
  return true;
}

bool readStrategy(const ConfigFile &file, const std::string &key, std::string &target,
                  std::string &error) {
  const ConfigValue *value = file.find(key);
  if (value == nullptr) {
    return true;
  }
  if (value->kind != ConfigValueKind::Text) {
    error = errorAt(value->line, key + " must be a string");
    return false;
  }
  if (value->text != "dfs" && value->text != "bfs" && value->text != "random" &&
      value->text != "coverage") {
    error = errorAt(value->line, key + " must be dfs, bfs, random or coverage");
    return false;
  }
  target = value->text;
  return true;
}

const std::vector<std::string> &knownKeys() {
  static const std::vector<std::string> keys = {"search.strategy",
                                                "search.max_states",
                                                "search.max_steps",
                                                "search.max_forks_per_path",
                                                "search.max_call_depth",
                                                "search.max_seconds",
                                                "search.seed",
                                                "input.buffer_elements",
                                                "input.random_count",
                                                "limits.max_tests",
                                                "limits.symbolic_offset_limit",
                                                "checks.shift_amount",
                                                "solver.independence",
                                                "solver.cache",
                                                "solver.conflict_budget"};
  return keys;
}

}

bool applySettings(const ConfigFile &file, Settings &settings, std::string &error) {
  for (const std::string &key : file.keysInOrder()) {
    bool known = false;
    for (const std::string &candidate : knownKeys()) {
      if (candidate == key) {
        known = true;
        break;
      }
    }
    if (!known) {
      const ConfigValue *value = file.find(key);
      error = errorAt(value == nullptr ? 0 : value->line, "unknown setting " + key);
      return false;
    }
  }
  if (!readStrategy(file, "search.strategy", settings.strategy, error) ||
      !readInteger(file, "search.max_states", 1, 1000000, settings.maxStates, error) ||
      !readInteger(file, "search.max_steps", 1, 1000000000, settings.maxSteps, error) ||
      !readInteger(file, "search.max_forks_per_path", 1, 1000000, settings.maxForksPerPath,
                   error) ||
      !readInteger(file, "search.max_call_depth", 1, 4096, settings.maxCallDepth, error) ||
      !readInteger(file, "search.seed", 0, 1000000000000LL, settings.seed, error) ||
      !readInteger(file, "input.buffer_elements", 1, 4096, settings.bufferElements, error) ||
      !readInteger(file, "input.random_count", 1, 100000000, settings.randomCount, error) ||
      !readInteger(file, "limits.max_tests", 1, 1000000, settings.maxTests, error) ||
      !readInteger(file, "limits.symbolic_offset_limit", 1, 1000000, settings.symbolicOffsetLimit,
                   error) ||
      !readInteger(file, "solver.conflict_budget", -1, 1000000000, settings.solverConflictBudget,
                   error) ||
      !readNumber(file, "search.max_seconds", 0.001, 100000.0, settings.maxSeconds, error) ||
      !readBoolean(file, "checks.shift_amount", settings.checkShiftAmount, error) ||
      !readBoolean(file, "solver.independence", settings.solverIndependence, error) ||
      !readBoolean(file, "solver.cache", settings.solverCache, error)) {
    return false;
  }
  return true;
}

bool loadSettings(const std::string &path, Settings &settings, std::string &error) {
  if (!fileExists(path)) {
    return true;
  }
  ConfigFile file;
  if (!file.parse(readFileOrDie(path), error)) {
    return false;
  }
  return applySettings(file, settings, error);
}

}
