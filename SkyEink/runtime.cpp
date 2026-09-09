// runtime.cpp

#include <Arduino.h>   // for RTC_DATA_ATTR
#include <string.h>
#include "runtime.h"

#define RT_MAGIC 0x534B5254UL   // "SKRT"

RTC_DATA_ATTR SkyRuntime rt;

void rtInit() {
  memset(&rt, 0, sizeof(rt));
  rt.magic = RT_MAGIC;
  rt.scanMsPerObject100 = 4000;   // 40 ms per object, conservative first guess
}

bool rtValid() { return rt.magic == RT_MAGIC; }

void rtSortPasses() {
  // Few elements, so insertion sort. Ordered by rise time.
  for (uint8_t i = 1; i < rt.passCount; i++) {
    PassRec key = rt.passes[i];
    int8_t j = (int8_t)i - 1;
    while (j >= 0 && rt.passes[j].tRise > key.tRise) {
      rt.passes[j + 1] = rt.passes[j];
      j--;
    }
    rt.passes[j + 1] = key;
  }
}

int rtNextPassIndex(time_t now) {
  for (uint8_t i = 0; i < rt.passCount; i++)
    if (rt.passes[i].tSet > now) return (int)i;
  return -1;
}
