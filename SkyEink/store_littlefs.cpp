// store_littlefs.cpp
//
// IMPORTANT: this file must never include Inkplate.h or SdFat. SdFat declares
// a global class File, FS.h does using fs::File, and the compiler rejects that
// as a conflicting declaration. A separate translation unit is the only clean
// fix, because the clash happens inside third party headers.

#include "board_config.h"
#ifdef BOARD_INKPLATE5V2

#include <Arduino.h>
#include <LittleFS.h>
#include "store_lfs.h"

namespace lfs {

namespace {
bool mounted = false;
}

bool begin() {
  if (mounted) return true;
  // true = format on first use
  mounted = LittleFS.begin(true);
  return mounted;
}

int32_t freeKb() {
  if (!mounted) return -1;
  size_t total = LittleFS.totalBytes();
  size_t used = LittleFS.usedBytes();
  if (total < used) return -1;
  return (int32_t)((total - used) / 1024);
}

void end() {
  if (!mounted) return;
  LittleFS.end();
  mounted = false;
}

bool exists(const char* path) {
  return mounted && LittleFS.exists(path);
}

size_t size(const char* path) {
  if (!mounted) return 0;
  File f = LittleFS.open(path, "r");
  if (!f) return 0;
  size_t s = (size_t)f.size();
  f.close();
  return s;
}

bool remove(const char* path)  { return mounted && LittleFS.remove(path); }

bool rename(const char* from, const char* to) {
  return mounted && LittleFS.rename(from, to);
}

bool mkdir(const char* path) {
  if (!mounted) return false;
  if (LittleFS.exists(path)) return true;
  return LittleFS.mkdir(path);
}

// The file object lives on the heap; the caller only sees a pointer.
void* openRead(const char* path) {
  if (!mounted) return nullptr;
  File* f = new File(LittleFS.open(path, "r"));
  if (!*f) { delete f; return nullptr; }
  return (void*)f;
}

void* openWrite(const char* path) {
  if (!mounted) return nullptr;
  File* f = new File(LittleFS.open(path, "w"));
  if (!*f) { delete f; return nullptr; }
  return (void*)f;
}

size_t read(void* h, void* buf, size_t len) {
  if (!h) return 0;
  return ((File*)h)->read((uint8_t*)buf, len);
}

size_t write(void* h, const void* buf, size_t len) {
  if (!h) return 0;
  return ((File*)h)->write((const uint8_t*)buf, len);
}

bool seek(void* h, uint32_t pos) {
  if (!h) return false;
  return ((File*)h)->seek(pos);
}

bool readLine(void* h, char* buf, size_t maxLen) {
  if (!h || maxLen < 2) return false;
  File* f = (File*)h;
  size_t i = 0;
  while (i < maxLen - 1 && f->available()) {
    char c = (char)f->read();
    buf[i++] = c;
    if (c == '\n') break;
  }
  buf[i] = 0;
  return i > 0;
}

void close(void* h) {
  if (!h) return;
  ((File*)h)->close();
  delete (File*)h;
}

}  // namespace lfs

#endif  // BOARD_INKPLATE5V2
