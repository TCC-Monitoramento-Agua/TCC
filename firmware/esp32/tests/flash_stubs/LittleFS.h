#pragma once
#include <algorithm>
#include <cstring>
#include <map>
#include <string>
#include <vector>
#include <stdint.h>
using std::min;
struct SerialMock { void println(const char*) {} };
static SerialMock Serial;
using Files = std::map<std::string, std::vector<uint8_t>>;
class File {
  Files* files = nullptr;
  std::string path;
  size_t pos = 0;
 public:
  File() = default;
  File(Files& f, const char* p) : files(&f), path(p) {}
  explicit operator bool() const { return files != nullptr; }
  size_t size() const { return files ? files->at(path).size() : 0; }
  bool seek(size_t n) { if (!files || n > size()) return false; pos = n; return true; }
  size_t read(uint8_t* data, size_t n) {
    if (!files) return 0;
    n = min(n, size()-pos);
    memcpy(data, files->at(path).data()+pos, n); pos += n; return n;
  }
  size_t write(const uint8_t* data, size_t n) {
    if (!files) return 0;
    auto& v = files->at(path);
    if (pos+n > v.size()) v.resize(pos+n);
    memcpy(v.data()+pos, data, n); pos += n; return n;
  }
  void flush() {}
  void close() { files = nullptr; }
};
class LittleFSMock {
 public:
  Files files;
  bool begin(bool, const char*, int, const char*) { return true; }
  bool format() { files.clear(); return true; }
  bool exists(const char* path) { return files.count(path); }
  File open(const char* path, const char* mode) {
    if (mode[0] == 'w') files[path].clear();
    if (!exists(path)) return File();
    return File(files, path);
  }
  bool rename(const char* from, const char* to) {
    if (!exists(from)) return false;
    files[to] = files[from]; files.erase(from); return true;
  }
};
static LittleFSMock LittleFS;
