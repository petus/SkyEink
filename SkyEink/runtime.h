// runtime.h - state that has to survive deep sleep.
//
// Lives in RTC RAM, so it is small, flat and free of pointers. Anything
// bigger belongs on the SD card.

#pragma once
#include <stdint.h>
#include <time.h>

#define SKY_MAX_PASSES 12
#define SKY_MAX_HOURS  16
#define SKY_NAME_LEN   20

struct PassRec {
  time_t   tRise;      // crossing above the minimum elevation
  time_t   tMax;
  time_t   tSet;
  int16_t  azRise;     // degrees
  int16_t  azMax;
  int16_t  azSet;
  int16_t  elMax;      // degrees
  float    mag;        // estimated visual magnitude at peak
  uint32_t norad;
  char     name[SKY_NAME_LEN];
  bool     notified;   // the per-pass wake-up already happened
  uint8_t  group;      // how many passes merged into this one, 1 = alone
  bool     mixed;      // merged passes had different names, i.e. a train
};

struct CloudRec {
  time_t  t0;                        // start of the first slot, whole hour UTC
  uint8_t hours;                     // number of valid slots
  uint8_t total[SKY_MAX_HOURS];      // total cloud cover, per cent
  int8_t  temp[SKY_MAX_HOURS];       // temperature, degrees Celsius
  uint8_t rain[SKY_MAX_HOURS];       // precipitation probability, per cent
  uint8_t mm10[SKY_MAX_HOURS];       // precipitation, tenths of a mm, max 25.5
};

struct SkyRuntime {
  uint32_t magic;

  uint8_t  passCount;
  PassRec  passes[SKY_MAX_PASSES];
  time_t   passesComputedAt;   // when the list was computed
  time_t   passesValidTo;      // how long it stays valid

  CloudRec cloud;
  time_t   cloudFetchedAt;

  time_t   gpFetchedAt;        // last element download
  time_t   gpEpochOldest;      // oldest epoch in the cache
  uint16_t gpCount;

  time_t   lastNtpAt;
  time_t   lastNetOkAt;
  uint8_t  netFailStreak;
  bool     storeFull;          // last download ran out of storage

  uint32_t bootCount;
  time_t   lastBootAt;         // time of the previous wake-up
  time_t   nextWakeAt;         // scheduled next wake-up, shown in the header
  uint8_t  lastScreen;         // see enum ScreenId
  uint32_t scanMsPerObject100; // measured SGP4 speed, hundredths of a ms
};

enum ScreenId : uint8_t {
  SCR_NIGHT = 0, SCR_DAY, SCR_PASS, SCR_STATUS, SCR_PROGRESS, SCR_PORTAL
};

// Defined in runtime.cpp with RTC_DATA_ATTR. This is only the declaration;
// the attribute is not repeated here.
extern SkyRuntime rt;

void rtInit();          // clears everything after a cold start
bool rtValid();
void rtSortPasses();
int  rtNextPassIndex(time_t now);   // -1 when none are left
