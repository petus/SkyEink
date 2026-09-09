// weather.h - hourly cloud cover from Open-Meteo.
//
// No key and no registration. Cloud cover gates the whole screen: the
// brightest pass is useless under an overcast, which is why this band sits at
// the very top.

#pragma once
#include <stdint.h>
#include <time.h>

namespace weather {

// Fetches the forecast for "hours" hours starting at "from" and fills
// rt.cloud. True on success.
bool fetch(time_t from, uint8_t hours);

// Slot index for a given time, or -1 outside the fetched range.
int slotFor(time_t t);

// Lowest and highest temperature in the fetched data. False when there is
// no data at all.
bool tempRange(int8_t& lo, int8_t& hi);

// Highest precipitation probability in the fetched data.
uint8_t maxRain();

// Total precipitation in millimetres over the whole window.
float totalMm();

// Data age in hours. 0xFFFF when there is none.
uint16_t ageHours(time_t now);

}  // namespace weather
