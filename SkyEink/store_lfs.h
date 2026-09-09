// store_lfs.h - a thin wrapper around LittleFS.
//
// It exists because of one clash: SdFat inside InkplateLibrary declares a
// global class File, and FS.h in the ESP32 core does using fs::File. The two
// cannot coexist in one translation unit, so board_inkplate5v2.cpp includes
// Inkplate.h and store_littlefs.cpp includes LittleFS.h, never both.
//
// This header therefore includes neither. Files are handed around as opaque
// pointers.

#pragma once
#include <stdint.h>
#include <stddef.h>

namespace lfs {

bool   begin();          // mounts, formatting on first use
int32_t freeKb();        // free space in KiB, -1 on error
void   end();
bool   exists(const char* path);
size_t size(const char* path);
bool   remove(const char* path);
bool   rename(const char* from, const char* to);
bool   mkdir(const char* path);

void*  openRead(const char* path);
void*  openWrite(const char* path);
size_t read(void* h, void* buf, size_t len);
size_t write(void* h, const void* buf, size_t len);
bool   seek(void* h, uint32_t pos);
bool   readLine(void* h, char* buf, size_t maxLen);
void   close(void* h);

}  // namespace lfs
