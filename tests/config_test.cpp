#include "util/config.h"

#include <string>

#include "tests/test_util.h"

namespace {

bool apply(const std::string &text, symx::Settings &settings, std::string &error) {
  symx::ConfigFile file;
  if (!file.parse(text, error)) {
    return false;
  }
  return symx::applySettings(file, settings, error);
}

SYMX_TEST(ValidSettingsAreApplied) {
  symx::Settings settings;
  std::string error;
  bool ok = apply(
      "[search]\n"
      "strategy = \"dfs\"\n"
      "max_states = 25\n"
      "max_seconds = 1.5\n"
      "\n"
      "[solver]\n"
      "cache = false\n",
      settings, error);
  CHECK(ok);
  CHECK(settings.strategy == "dfs");
  CHECK_EQ(settings.maxStates, 25LL);
  CHECK(settings.maxSeconds == 1.5);
  CHECK(!settings.solverCache);
  CHECK(settings.solverIndependence);
}

SYMX_TEST(CommentsAndBlankLinesAreIgnored) {
  symx::Settings settings;
  std::string error;
  bool ok = apply(
      "# leading note\n"
      "\n"
      "[input]\n"
      "buffer_elements = 3 # trailing note\n",
      settings, error);
  CHECK(ok);
  CHECK_EQ(settings.bufferElements, 3LL);
}

SYMX_TEST(UnknownKeysAreRejected) {
  symx::Settings settings;
  std::string error;
  CHECK(!apply("[search]\nspeed = 4\n", settings, error));
  CHECK(error.find("unknown setting search.speed") != std::string::npos);
}

SYMX_TEST(WrongTypesAreRejected) {
  symx::Settings settings;
  std::string error;
  CHECK(!apply("[search]\nmax_states = \"many\"\n", settings, error));
  CHECK(error.find("must be an integer") != std::string::npos);
  CHECK(!apply("[solver]\ncache = 1\n", settings, error));
  CHECK(error.find("must be true or false") != std::string::npos);
}

SYMX_TEST(OutOfRangeValuesAreRejected) {
  symx::Settings settings;
  std::string error;
  CHECK(!apply("[search]\nmax_states = 0\n", settings, error));
  CHECK(error.find("must be between") != std::string::npos);
  CHECK(!apply("[input]\nbuffer_elements = 100000\n", settings, error));
  CHECK(error.find("must be between") != std::string::npos);
}

SYMX_TEST(BadStrategyNamesAreRejected) {
  symx::Settings settings;
  std::string error;
  CHECK(!apply("[search]\nstrategy = \"greedy\"\n", settings, error));
  CHECK(error.find("dfs, bfs, random or coverage") != std::string::npos);
}

SYMX_TEST(MalformedLinesAreRejected) {
  symx::Settings settings;
  std::string error;
  CHECK(!apply("[search\nmax_states = 4\n", settings, error));
  CHECK(!apply("max_states\n", settings, error));
  CHECK(!apply("[search]\nstrategy = \"dfs\nmax_states = 4\n", settings, error));
}

SYMX_TEST(DuplicateKeysAreRejected) {
  symx::Settings settings;
  std::string error;
  CHECK(!apply("[search]\nmax_states = 4\nmax_states = 5\n", settings, error));
  CHECK(error.find("duplicate key") != std::string::npos);
}

SYMX_TEST(AMissingFileLeavesDefaults) {
  symx::Settings settings;
  std::string error;
  CHECK(symx::loadSettings("no-such-config-file.toml", settings, error));
  CHECK(settings.strategy == "coverage");
  CHECK_EQ(settings.maxStates, 4000LL);
}

}

SYMX_TEST_MAIN("config_test")
