// screens.h - drawing of the individual screens.
//
// None of these functions refreshes the display. That is up to the caller,
// which is the only place that knows whether a full or a partial refresh is
// wanted.

#pragma once
#include <stdint.h>
#include <time.h>

namespace screens {

// The main night screen: cloud cover, passes, planets and the Moon.
void night(time_t now);

// Daytime summary, shown while the Sun is up.
void day(time_t now);

// Detail of one pass, or two that are close together, with a countdown.
void pass(time_t now, uint8_t index, uint8_t alsoIndex);

// Progress screen. Drawn in MODE_MONO and called repeatedly. There is
// deliberately no estimate of the remaining time: for a download it depends
// on the link and on how much the server sends, so the number would lie.
void progress(const char* stage, uint8_t pct);

// Status and faults. reason is a short string or nullptr.
void status(time_t now, const char* reason);

// Access point screen with the details needed to connect.
void portal(const char* ssid, const char* pass, const char* ip);

}  // namespace screens
