// gp.h - orbital elements from Celestrak.
//
// Downloaded as CSV, not TLE. Five digit catalogue numbers ran out in July
// 2026; new objects get six digit ones, and Celestrak does not serve those in
// TLE format. The two line elements are therefore assembled here from CSV,
// with a placeholder catalogue number, because SGP4 never looks at it.
//
// Since March 2026 Celestrak enforces one download per data update. A second
// request returns 403 and repeated attempts get the IP firewalled. This
// firmware fetches at most once a day and never retries after a 403.

#pragma once
#include <stdint.h>
#include <time.h>

namespace gp {

// Alongside the assembled two line elements we keep the raw numbers. The
// geometric pre-filter in passes.cpp uses them to tell from the orbital plane
// that an object cannot reach the site during a given night, which saves an
// expensive propagation.
struct Rec {
  char     name[20];
  uint32_t norad;
  float    stdMag;     // intrinsic magnitude at 1000 km, 90 deg phase angle
  float    inc;        // inclination, degrees
  float    raan;       // right ascension of the ascending node, degrees
  float    ecc;        // eccentricity
  float    mm;         // mean motion, revolutions per day
  int32_t  epoch;      // element epoch, Unix time
  char     l1[70];
  char     l2[70];
};

// Progress callback. stage is a short caption for the display, pct 0 to 100.
typedef void (*ProgressFn)(const char* stage, uint8_t pct);

// Downloads the enabled groups, filters them and stores them on the card.
// Returns the number of objects stored, or one of the codes below.
#define GP_ERR_NET   (-1)
#define GP_ERR_403   (-2)   // Celestrak: data unchanged since last time
#define GP_ERR_STORE (-3)   // storage missing or full
int refresh(ProgressFn cb);

// Cache size in bytes for the current settings. Used for the space check.
uint32_t cacheBytesNeeded();

// Working with the stored cache.
bool     open();
void     close();
uint16_t count();
bool     get(uint16_t index, Rec& out);

// Oldest epoch in the cache, used to watch the data age.
time_t   oldestEpoch();

// When the cache was downloaded, how many objects it holds and the oldest
// epoch in it. Stored next to the cache, so it survives a cold start with
// the RTC memory wiped: after a reflash or a battery swap the device can
// tell that yesterday's catalogue on the card is still good and skip an
// eighty second download. Returns false when there is no usable cache.
bool     cacheInfo(time_t& fetchedAt, uint16_t& count, time_t& oldest);

// Intrinsic magnitude guessed from the name. Public for testing.
float    guessStdMag(const char* name);

}  // namespace gp
