// ui.h - drawing helpers on top of the HAL.
//
// Adafruit GFX fonts only cover ASCII, so accented characters are drawn as
// the base letter with the mark added from primitives. The application writes
// plain UTF-8 and never has to think about it.

#pragma once
#include <stdint.h>
#include <time.h>
#include "hal.h"
#include "layout.h"

namespace ui {

using layout::Rect;

// Text with accents. Returns the width of what was drawn.
int16_t text(int16_t x, int16_t y, const char* utf8,
             hal::Align a, hal::Ink c);
int16_t textW(const char* utf8);

// Shortens the text to fit maxW and ends it with dots.
void fit(char* buf, size_t bufLen, int16_t maxW);

// Horizontal rule across the full content width.
void rule(int16_t y, hal::Ink c);

// Shaded fill. On an eight grey panel it uses a real level, on a one bit
// panel a dither pattern.
void shade(const Rect& r, hal::Ink c);

// Outlined bar filled to a fraction of 0 to 1, bottom up or top down.
void bar(const Rect& r, float frac, bool fromTop, hal::Ink fill);

// Time and number formatting.
void fmtClock(char* buf, size_t n, time_t utc);          // HH:MM local time
void fmtDate(char* buf, size_t n, time_t utc);           // "po 31. 8."
void fmtAge(char* buf, size_t n, time_t then, time_t now); // "4 h ago"
void fmtMag(char* buf, size_t n, float mag);             // "-3,8"
const char* compass(int16_t azDeg);                      // "ZSZ"

// State of charge from cell voltage. A lithium cell does not discharge along
// a straight line, hence a table with linear interpolation between points.
uint8_t battPercent(float volts);

// The header and the status bar are the same on every screen.
void drawHeader(const layout::Frame& f, const char* title, time_t now);
void drawStatus(const layout::Frame& f, time_t now);

}  // namespace ui
