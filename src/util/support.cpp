#include "util/support.h"

#include <sys/stat.h>
#include <sys/types.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>

#ifdef _WIN32
#include <direct.h>
#endif

namespace symx {

void fatal(const std::string &message) {
  std::cerr << "symx: fatal: " << message << "\n";
  std::exit(2);
}

std::string readFileOrDie(const std::string &path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    fatal("cannot open " + path);
  }
  std::ostringstream buffer;
  buffer << in.rdbuf();
  return buffer.str();
}

bool writeFile(const std::string &path, const std::string &contents) {
  std::ofstream out(path, std::ios::binary);
  if (!out) {
    return false;
  }
  out << contents;
  return out.good();
}

namespace {

bool createOneDirectory(const std::string &path) {
#ifdef _WIN32
  return _mkdir(path.c_str()) == 0;
#else
  return mkdir(path.c_str(), 0755) == 0;
#endif
}

}

bool makeDirectory(const std::string &path) {
  if (path.empty()) {
    return false;
  }
  std::string prefix;
  for (char c : path) {
    if (c == '/' || c == '\\') {
      if (!prefix.empty() && !fileExists(prefix)) {
        createOneDirectory(prefix);
      }
      prefix.push_back(c);
      continue;
    }
    prefix.push_back(c);
  }
  if (!fileExists(path)) {
    createOneDirectory(path);
  }
  return fileExists(path);
}

bool fileExists(const std::string &path) {
  struct stat info;
  return stat(path.c_str(), &info) == 0;
}

std::string joinPath(const std::string &directory, const std::string &name) {
  if (directory.empty()) {
    return name;
  }
  char last = directory[directory.size() - 1];
  if (last == '/' || last == '\\') {
    return directory + name;
  }
  return directory + "/" + name;
}

std::vector<std::string> splitLines(const std::string &text) {
  std::vector<std::string> lines;
  std::string current;
  for (char c : text) {
    if (c == '\n') {
      lines.push_back(current);
      current.clear();
    } else if (c != '\r') {
      current.push_back(c);
    }
  }
  lines.push_back(current);
  return lines;
}

std::string trim(const std::string &text) {
  size_t begin = 0;
  size_t end = text.size();
  while (begin < end && (text[begin] == ' ' || text[begin] == '\t')) {
    begin++;
  }
  while (end > begin && (text[end - 1] == ' ' || text[end - 1] == '\t')) {
    end--;
  }
  return text.substr(begin, end - begin);
}

bool startsWith(const std::string &text, const std::string &prefix) {
  return text.size() >= prefix.size() && text.compare(0, prefix.size(), prefix) == 0;
}

bool endsWith(const std::string &text, const std::string &suffix) {
  return text.size() >= suffix.size() &&
         text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::string jsonEscape(const std::string &text) {
  std::string out;
  for (char c : text) {
    switch (c) {
      case '"':
        out += "\\\"";
        break;
      case '\\':
        out += "\\\\";
        break;
      case '\n':
        out += "\\n";
        break;
      case '\t':
        out += "\\t";
        break;
      case '\r':
        out += "\\r";
        break;
      default:
        if (static_cast<unsigned char>(c) < 0x20) {
          char buffer[8];
          std::snprintf(buffer, sizeof(buffer), "\\u%04x", static_cast<unsigned char>(c));
          out += buffer;
        } else {
          out.push_back(c);
        }
    }
  }
  return out;
}

}
