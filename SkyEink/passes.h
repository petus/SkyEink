// passes.h - visible pass prediction.
//
// "The satellite is above the horizon" is not enough. It also has to be lit
// by the Sun, the observer has to be in the dark, and the estimated magnitude
// has to be under the configured limit. Without all three you get hundreds of
// passes, five of which are actually visible.

#pragma once
#include <stdint.h>
#include <time.h>
#include "runtime.h"

namespace passes {

typedef void (*ProgressFn)(const char* stage, uint8_t pct);

// Computes the passes in the darkness window nearest to "around" and fills
// rt.passes. Returns how many were found, -1 on an element cache error.
int compute(time_t around, ProgressFn cb);

// Stores the computed list on the card, and reads it back.
//
// The list lives in RTC memory, which a cold start wipes. Without this, every
// reflash, battery swap or brownout costs a full minute of recomputation for
// a night that has already been worked out. The file carries a fingerprint of
// the settings the list was computed with, so changing the magnitude limit or
// the site coordinates invalidates it.
void savePasses();
bool loadPasses(time_t now);

// Stores the computed list on the card, and reads it back.
//
// The list lives in RTC memory, which a cold start wipes. Without this, every
// reflash, battery swap or brownout costs a full minute of recomputation for
// a night that has already been worked out. The file carries a fingerprint of
// the settings the list was computed with, so changing the magnitude limit or
// the site coordinates invalidates it.
void savePasses();
bool loadPasses(time_t now);

// Ground track drawing on the pass detail screen. trackBegin() locates the
// object in the cache once, then trackAt() is called per point. Without the
// split, every point would rescan the whole cache.
bool trackBegin(uint16_t passIndex);
bool trackAt(time_t t, float& azOut, float& elOut);
void trackEnd();

}  // namespace passes
