// config.h - persistent settings in NVS.

#pragma once
#include <stdint.h>

// Bump the magic on ANY change to the Config struct. An old configuration is
// then discarded instead of being reinterpreted byte by byte.
#define CFG_MAGIC 0x534B4532UL   // "SKE2", bumped when the language was added

// Celestrak element groups.
#define GRP_VISUAL    0x01
#define GRP_STATIONS  0x02
#define GRP_LAST30    0x04
#define GRP_STARLINK  0x08

struct SkyConfig {
  uint32_t magic;

  char  ssid[33];
  char  pass[65];

  float lat;          // degrees, positive north
  float lon;          // degrees, positive east
  float altM;         // site altitude above sea level

  char  tz[40];       // POSIX TZ string, defaults to Central European

  uint8_t  groups;        // GRP_* bit mask
  uint16_t maxObjects;    // upper bound on propagated objects
  float    magLimit;      // only passes brighter than this make the list
  uint8_t  minElevDeg;    // minimum peak elevation of a pass
  float    slMinMeanMotion;  // Starlink filter, revolutions per day

  uint8_t  maxEvents;     // per-pass wake-ups allowed per night
  bool     eventWakes;    // wake for passes at all
  uint16_t quietFromMin;  // quiet hours, minutes past midnight
  uint16_t quietToMin;

  uint8_t  sunSwitchDeg;  // solar elevation below which night mode starts
  uint8_t  syncHourUtc;   // hour of the morning sync, UTC
  uint8_t  lang;          // 0 Czech, 1 English
};

extern SkyConfig cfg;

void cfgDefaults(SkyConfig& c);
void cfgSanitize(SkyConfig& c);
bool cfgLoad();          // false = nothing stored, defaults were used
bool cfgSave();          // verifies the write by reading it back
void cfgErase();
bool cfgHasWifi();
