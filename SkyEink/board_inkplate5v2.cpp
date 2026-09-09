// board_inkplate5v2.cpp - the only file in this project that knows the
// hardware.
//
// Porting to another board means writing one more file like this and flipping
// a define in board_config.h. Nothing else changes.
//
// Verified against the Soldered-Inkplate-5-Gen2-hardware-design V1.1.0
// schematic:
//   WAKE button       GPIO36  (RTC_GPIO0, input only, ext0 capable)
//   PCF85063A INT     GPIO39  (RTC_GPIO3, input only, ext0 capable)
//   CLKOUT RTC        pouze testpoint, do MCU nevede
//
// Note: the classic ESP32 only offers ALL_LOW and ANY_HIGH for ext1. Both the
// button and INT are active low, so "either of them" cannot be expressed.
// ext0 therefore watches the button, and timing is handled by the timer with
// the drift corrected against the RTC.

#include "board_config.h"
#ifdef BOARD_INKPLATE5V2

#include <Arduino.h>
#include <Inkplate.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <esp_sleep.h>
#include "store_lfs.h"   // fallback in internal flash, see store_littlefs.cpp
#include <qrcode.h>      // generator from ESP-IDF, espressif/qrcode
#include <time.h>
#include <stdlib.h>   // setenv, unsetenv
#include <math.h>

// The fonts live in the root of InkplateLibrary rather than under src/, so
// the Arduino IDE never puts them on the include path. The official Soldered
// examples solve it the same way, with a copy of the fonts in the sketch
// folder. Hence quotes rather than angle brackets.
#include "Fonts/FreeSans9pt7b.h"
#include "Fonts/FreeSans12pt7b.h"
#include "Fonts/FreeSans18pt7b.h"
#include "Fonts/FreeSans24pt7b.h"

#include "hal.h"
#include "version.h"
#include "astro.h"   // tmToUtc, pure maths with no hardware dependency

#define PIN_WAKE   36
#define PIN_RTCINT 39

// A long sleep is split into chunks. After each one the time is realigned
// against the RTC, so the drift of the internal oscillator does not add up.
#define SLEEP_CHUNK_S 3600

namespace hal {

namespace {

Inkplate ink(INKPLATE_3BIT);
DispCaps caps;
Metrics  met;
DrawMode mode = MODE_GRAY;
bool     storeReady = false;
bool     useSd      = false;   // false = LittleFS in internal flash
WakeReason wake = WAKE_POWERON;

RTC_DATA_ATTR time_t targetWake = 0;      // where we actually want to end up
RTC_DATA_ATTR time_t lastNtp    = 0;
RTC_DATA_ATTR bool   rtcTrusted = false;

// Eight levels: 0 is black, 7 is white.
const uint8_t GRAY8[5] = {7, 6, 4, 2, 0};   // WHITE, LIGHT, MID, DARK, BLACK

// Dither for one bit mode. A 4x4 Bayer matrix, thresholds at the quarters.
const uint8_t BAYER[4][4] = {
  { 0,  8,  2, 10},
  {12,  4, 14,  6},
  { 3, 11,  1,  9},
  {15,  7, 13,  5}
};

// True when the pixel should be black.
inline bool ditherOn(int16_t x, int16_t y, Ink c) {
  switch (c) {
    case INK_WHITE: return false;
    case INK_BLACK: return true;
    case INK_LIGHT: return BAYER[y & 3][x & 3] < 4;
    case INK_MID:   return BAYER[y & 3][x & 3] < 8;
    case INK_DARK:  return BAYER[y & 3][x & 3] < 12;
  }
  return true;
}

inline uint8_t colorFor(Ink c) {
  return (mode == MODE_GRAY) ? GRAY8[c] : (c == INK_WHITE ? 0 : 1);
}

// In one bit mode the intermediate inks are drawn pixel by pixel.
inline bool needsDither(Ink c) {
  return mode == MODE_MONO && c != INK_WHITE && c != INK_BLACK;
}

const GFXfont* fontFor(FontSize f) {
  // The choice follows the panel height, so a port to a smaller display
  // needs no change to the layout.
  bool big = caps.h >= 600;
  switch (f) {
    case FONT_XS: return big ? &FreeSans9pt7b  : &FreeSans9pt7b;
    case FONT_S:  return big ? &FreeSans12pt7b : &FreeSans9pt7b;
    case FONT_M:  return big ? &FreeSans12pt7b : &FreeSans9pt7b;
    case FONT_L:  return big ? &FreeSans18pt7b : &FreeSans12pt7b;
    case FONT_XL: return big ? &FreeSans24pt7b : &FreeSans18pt7b;
  }
  return &FreeSans12pt7b;
}

const GFXfont* curFont = nullptr;
int16_t curAscent = 12;
int16_t curHeight = 18;

// Cursor advance for one character. Adafruit GFX keeps it in the glyph as
// xAdvance. It must not come from getTextBounds(), which returns the bounding
// box of the drawn pixels, so a space measures zero and the text runs
// together.
int16_t advanceOf(char c) {
  if (!curFont) return 6;
  uint16_t first = curFont->first;
  uint16_t last  = curFont->last;
  uint8_t  u     = (uint8_t)c;
  if (u < first || u > last) return (int16_t)(curFont->yAdvance / 2);
  // The library keeps pgm_read_glyph_ptr() as a static function in its own
  // .cpp, so it is not visible from the header. On ESP32 the flash is memory
  // mapped, so plain pointer arithmetic is enough.
  const GFXglyph* g = curFont->glyph + (u - first);
  return (int16_t)pgm_read_byte(&g->xAdvance);
}

}  // namespace

// ---------------------------------------------------------------- displej

const DispCaps& dispCaps() { return caps; }
const Metrics&  metrics()  { return met; }

bool dispBegin() {
  ink.begin();
  // Landscape. The panel is natively portrait, so the rotation follows what
  // the board reports after begin() rather than a hard coded value.
  if (ink.width() < ink.height()) ink.setRotation(1);

  caps.w = (int16_t)ink.width();
  caps.h = (int16_t)ink.height();
  caps.grayLevels   = 8;
  caps.partial      = true;
  caps.persistent   = true;
  caps.fullRefreshMs = 1100;
  caps.partRefreshMs = 250;

  met.scale = (float)caps.h / 720.0f;
  met.pad   = (int16_t)(12 * met.scale); if (met.pad < 10) met.pad = 10;
  met.gap   = (int16_t)(10 * met.scale); if (met.gap < 8) met.gap = 8;
  // The lower bound on row height is not cosmetic. The smallest font has a
  // line height of 22 px and the layout derives every band height from rowH,
  // so below this value the text overflows whatever the panel size.
  met.rowH  = (int16_t)(30 * met.scale); if (met.rowH < 24) met.rowH = 24;
  met.ruleW = 2;

  setFont(FONT_M);
  return true;
}

void dispEnd() { ink.einkOff(); }

bool dispSetMode(DrawMode m) {
  if (m == mode) return true;
  ink.setDisplayMode(m == MODE_GRAY ? INKPLATE_3BIT : INKPLATE_1BIT);
  mode = m;
  return true;
}

DrawMode dispMode() { return mode; }

void dispClear(Ink bg) {
  ink.clearDisplay();
  if (bg != INK_WHITE) fillRect(0, 0, caps.w, caps.h, bg);
}

void dispFlushFull()    { ink.display(); }
void dispFlushPartial() {
  // Eight greys and partial refresh are mutually exclusive on Inkplate.
  if (mode == MODE_MONO) ink.partialUpdate();
  else                   ink.display();
}

void drawPixel(int16_t x, int16_t y, Ink c) {
  if (needsDither(c)) {
    if (ditherOn(x, y, c)) ink.drawPixel(x, y, 1);
    return;
  }
  ink.drawPixel(x, y, colorFor(c));
}

void drawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, Ink c) {
  if (needsDither(c)) {
    // Bresenham pixel by pixel, so the dither pattern lines up.
    int16_t dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int16_t dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int16_t err = dx + dy;
    while (true) {
      drawPixel(x0, y0, c);
      if (x0 == x1 && y0 == y1) break;
      int16_t e2 = (int16_t)(2 * err);
      if (e2 >= dy) { err = (int16_t)(err + dy); x0 = (int16_t)(x0 + sx); }
      if (e2 <= dx) { err = (int16_t)(err + dx); y0 = (int16_t)(y0 + sy); }
    }
    return;
  }
  ink.drawLine(x0, y0, x1, y1, colorFor(c));
}

void drawHLine(int16_t x, int16_t y, int16_t len, Ink c) {
  if (needsDither(c)) { for (int16_t i = 0; i < len; i++) drawPixel((int16_t)(x + i), y, c); return; }
  ink.drawFastHLine(x, y, len, colorFor(c));
}

void drawVLine(int16_t x, int16_t y, int16_t len, Ink c) {
  if (needsDither(c)) { for (int16_t i = 0; i < len; i++) drawPixel(x, (int16_t)(y + i), c); return; }
  ink.drawFastVLine(x, y, len, colorFor(c));
}

void drawRect(int16_t x, int16_t y, int16_t w, int16_t h, Ink c) {
  if (needsDither(c)) {
    drawHLine(x, y, w, c); drawHLine(x, (int16_t)(y + h - 1), w, c);
    drawVLine(x, y, h, c); drawVLine((int16_t)(x + w - 1), y, h, c);
    return;
  }
  ink.drawRect(x, y, w, h, colorFor(c));
}

void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, Ink c) {
  if (needsDither(c)) {
    for (int16_t j = 0; j < h; j++)
      for (int16_t i = 0; i < w; i++)
        drawPixel((int16_t)(x + i), (int16_t)(y + j), c);
    return;
  }
  ink.fillRect(x, y, w, h, colorFor(c));
}

void drawCircle(int16_t cx, int16_t cy, int16_t r, Ink c) {
  if (needsDither(c)) {
    // Midpoint po bodech.
    int16_t x = r, y = 0, err = 0;
    while (x >= y) {
      drawPixel((int16_t)(cx + x), (int16_t)(cy + y), c);
      drawPixel((int16_t)(cx + y), (int16_t)(cy + x), c);
      drawPixel((int16_t)(cx - y), (int16_t)(cy + x), c);
      drawPixel((int16_t)(cx - x), (int16_t)(cy + y), c);
      drawPixel((int16_t)(cx - x), (int16_t)(cy - y), c);
      drawPixel((int16_t)(cx - y), (int16_t)(cy - x), c);
      drawPixel((int16_t)(cx + y), (int16_t)(cy - x), c);
      drawPixel((int16_t)(cx + x), (int16_t)(cy - y), c);
      y++;
      if (err <= 0) err = (int16_t)(err + 2 * y + 1);
      if (err >  0) { x--; err = (int16_t)(err - 2 * x + 1); }
    }
    return;
  }
  ink.drawCircle(cx, cy, r, colorFor(c));
}

void fillCircle(int16_t cx, int16_t cy, int16_t r, Ink c) {
  if (needsDither(c)) {
    for (int16_t dy = (int16_t)-r; dy <= r; dy++) {
      int16_t dx = (int16_t)lround(sqrt((double)(r * r - dy * dy)));
      drawHLine((int16_t)(cx - dx), (int16_t)(cy + dy), (int16_t)(2 * dx + 1), c);
    }
    return;
  }
  ink.fillCircle(cx, cy, r, colorFor(c));
}

void drawArc(int16_t cx, int16_t cy, int16_t r,
             float a0deg, float a1deg, Ink c) {
  const float step = 1.0f;
  for (float a = a0deg; a <= a1deg; a += step) {
    float rad = a * (float)M_PI / 180.0f;
    drawPixel((int16_t)lround(cx + r * cos(rad)),
              (int16_t)lround(cy + r * sin(rad)), c);
  }
}

void drawDashed(int16_t x0, int16_t y0, int16_t x1, int16_t y1,
                uint8_t on, uint8_t off, Ink c) {
  float dx = (float)(x1 - x0), dy = (float)(y1 - y0);
  float len = sqrtf(dx * dx + dy * dy);
  if (len < 1.0f) return;
  dx /= len; dy /= len;
  float t = 0;
  while (t < len) {
    float e = t + on;
    if (e > len) e = len;
    drawLine((int16_t)lround(x0 + dx * t), (int16_t)lround(y0 + dy * t),
             (int16_t)lround(x0 + dx * e), (int16_t)lround(y0 + dy * e), c);
    t = e + off;
  }
}

// The generator calls back into a drawing function, so the target is passed
// through these variables. The call is not reentrant, which is fine here.
namespace {
int16_t qrX, qrY, qrSide;

void qrPaint(esp_qrcode_handle_t qrcode) {
  int n = esp_qrcode_get_size(qrcode);
  if (n <= 0) return;

  // Two modules of quiet zone on each side.
  int16_t px = (int16_t)(qrSide / (n + 4));
  if (px < 1) px = 1;
  int16_t off = (int16_t)((qrSide - n * px) / 2);

  fillRect(qrX, qrY, qrSide, qrSide, INK_WHITE);
  for (int y = 0; y < n; y++)
    for (int x = 0; x < n; x++)
      if (esp_qrcode_get_module(qrcode, x, y))
        fillRect((int16_t)(qrX + off + x * px), (int16_t)(qrY + off + y * px),
                 px, px, INK_BLACK);
}
}  // namespace

bool drawQr(int16_t x, int16_t y, int16_t side, const char* text) {
  if (!text || !text[0] || side < 16) return false;
  qrX = x; qrY = y; qrSide = side;

  esp_qrcode_config_t qcfg = ESP_QRCODE_CONFIG_DEFAULT();
  qcfg.display_func = qrPaint;
  qcfg.max_qrcode_version = 6;
  return esp_qrcode_generate(&qcfg, text) == ESP_OK;
}

void setFont(FontSize f) {
  const GFXfont* g = fontFor(f);
  curFont = g;
  ink.setFont(g);
  ink.setTextSize(1);
  ink.setTextWrap(false);
  // Cap height and line spacing are tracked here, because getTextBounds is
  // needlessly expensive for this.
  curAscent = (int16_t)(g->yAdvance * 3 / 4);
  curHeight = (int16_t)g->yAdvance;
}

void drawText(int16_t x, int16_t y, const char* s, Align a, Ink c) {
  int16_t w = textWidth(s);
  int16_t x0 = x;
  if (a == ALIGN_CENTER) x0 = (int16_t)(x - w / 2);
  else if (a == ALIGN_RIGHT) x0 = (int16_t)(x - w);
  ink.setTextColor(colorFor(c));
  ink.setCursor(x0, y);
  ink.print(s);
}

int16_t textWidth(const char* s) {
  int16_t w = 0;
  for (const char* p = s; *p; p++) w = (int16_t)(w + advanceOf(*p));
  return w;
}

int16_t textHeight() { return curHeight; }
int16_t textAscent() { return curAscent; }

// ---------------------------------------------------------------- time

// A fixed summer instant, for when the clock is not running yet.
static const time_t TZ_PROBE = 1788262736;   // 2026-09-01 11:38:56 UTC

long applyTimezone(const char* posixTz) {
  if (!posixTz || !posixTz[0]) posixTz = "UTC0";
  setenv("TZ", posixTz, 1);
  tzset();

  // The offset is derived by decomposing a concrete instant rather than by
  // reading the string, which also catches a nonsense string that makes the
  // library fall back to UTC without saying so.
  //
  // tm_gmtoff and tm_zone are glibc extensions that newlib on ESP32 does not
  // have, so the offset is the difference between the local and the UTC
  // decomposition.
  time_t t = timeNow();
  if (!t) t = TZ_PROBE;
  struct tm lt;
  localtime_r(&t, &lt);
  return (long)(astro::tmToUtc(lt) - t);
}

const char* timezoneName() {
  static char name[8];
  time_t t = timeNow();
  if (!t) t = TZ_PROBE;
  struct tm lt;
  localtime_r(&t, &lt);
  if (strftime(name, sizeof(name), "%Z", &lt) == 0) strcpy(name, "?");
  return name;
}

// InkplateLibrary converts an epoch into RTC registers with localtime() and
// back with mktime(). Both honour the TZ environment variable, so the round
// trip only comes out right when TZ is the same on write and on read.
//
// That is easy to break: configTime() overwrites TZ with UTC, so a clock
// written during a sync is stored as universal time but read back as local
// time, and the epoch comes out two hours low.
//
// The fix is to pin TZ to UTC for the whole time the RTC is touched. The
// registers then always hold universal time, whatever the surroundings.
class TzUtc {
  public:
    TzUtc() {
      const char* tz = getenv("TZ");
      saved[0] = 0;
      if (tz) strncpy(saved, tz, sizeof(saved) - 1);
      setenv("TZ", "UTC0", 1);
      tzset();
    }
    ~TzUtc() {
      if (saved[0]) setenv("TZ", saved, 1);
      else          unsetenv("TZ");
      tzset();
    }
  private:
    char saved[48];
};

bool rtcPresent() { return true; }

bool timeValid() { return timeNow() != 0; }

time_t timeNow() {
  TzUtc guard;
  if (!ink.rtcIsSet()) return 0;
  uint32_t e = ink.rtcGetEpoch();
  return e > 1735689600UL ? (time_t)e : 0;
}

void timeSet(time_t utc) {
  {
    TzUtc guard;
    ink.rtcSetEpoch((uint32_t)utc);
  }
  struct timeval tv = {(time_t)utc, 0};
  settimeofday(&tv, nullptr);
  lastNtp = utc;
  rtcTrusted = true;
}

uint32_t timeErrorEstimateSec() {
  if (!lastNtp) return 0xFFFFFFFF;
  // PCF85063A with a crystal: a conservative 2 s per week.
  time_t now = timeNow();
  if (!now || now < lastNtp) return 0;
  return (uint32_t)((now - lastNtp) * 2 / 604800) + 1;
}

// ---------------------------------------------------------------- sleep

WakeReason wakeReason() { return wake; }

[[noreturn]] void idleUntil(time_t utcWake) {
  targetWake = utcWake;

  time_t now = timeNow();
  long delta = now ? (long)(utcWake - now) : 60;
  if (delta < 5) delta = 5;

  // A long sleep is split up. After each wake-up the time is read from the
  // RTC and the remainder recomputed, so the timer error does not accumulate.
  long chunk = delta > SLEEP_CHUNK_S ? SLEEP_CHUNK_S : delta;

  // The order matters. The card and the radio draw current even when idle,
  // and deep sleep only cuts them a moment later. Shutting them down by hand
  // costs a few milliseconds and saves considerably more.
  storeEnd();
  netDisconnect();
  dispEnd();

  esp_sleep_enable_timer_wakeup((uint64_t)chunk * 1000000ULL);
  esp_sleep_enable_ext0_wakeup((gpio_num_t)PIN_WAKE, 0);   // button pulls low
  esp_deep_sleep_start();
  while (true) {}   // sem se nedostaneme
}

// ---------------------------------------------------------------- buttons

uint8_t buttonsPresent() { return 1 << BTN_A; }   // WAKE is the only one

bool buttonCanWake(Button b) { return b == BTN_A; }

bool buttonDown(Button b) {
  if (b != BTN_A) return false;
  return digitalRead(PIN_WAKE) == LOW;
}

uint32_t buttonHeldMs(Button b) {
  if (b != BTN_A || !buttonDown(b)) return 0;
  uint32_t t0 = millis();
  while (buttonDown(b)) {
    delay(20);
    if (millis() - t0 > 8000) break;
  }
  return millis() - t0;
}

// ---------------------------------------------------------------- storage

// microSD first, because it is large and there is no need to be frugal with
// it. When the card is missing or fails to start, the code falls back to
// LittleFS in internal flash. At 1200 objects the elements take about 200 kB,
// which fits a file system partition. Writes happen once a day, so flash wear
// is negligible.

bool storeBegin() {
  if (storeReady) return true;

  if (ink.sdCardInit()) {
    useSd = true;
    storeReady = true;
    storeMkdir("/" FW_SLUG);
    return true;
  }

  if (lfs::begin()) {
    useSd = false;
    storeReady = true;
    storeMkdir("/" FW_SLUG);
    return true;
  }

  return false;
}

namespace {

// InkplateLibrary bundles SdFat, and the two copies inside it have diverged.
// The older one (src/libs/sdfat) has blocksPerCluster(), the newer one
// (src/features/SdFat) has bytesPerCluster(). There is no common name.
//
// The overload pair below lets the compiler decide. The first is only viable
// when bytesPerCluster() actually exists, because otherwise the decltype
// expression fails to compile and the overload quietly drops out of the set.
// The second takes a long instead of an int, so it ranks second and acts as
// the fallback.
template <typename V>
auto clusterBytes(V* v, int) -> decltype(v->bytesPerCluster(), uint32_t()) {
  return (uint32_t)v->bytesPerCluster();
}

template <typename V>
auto clusterBytes(V* v, long) -> uint32_t {
  return (uint32_t)v->blocksPerCluster() * 512UL;   // a block is always 512 B
}

}  // namespace

int32_t storeFreeKb() {
  if (!storeBegin()) return -1;

  if (useSd) {
    int32_t clusters = (int32_t)ink.getSdFat().vol()->freeClusterCount();
    if (clusters < 0) return -1;
    uint32_t bpc = clusterBytes(ink.getSdFat().vol(), 0);
    if (!bpc) return -1;
    return (int32_t)(((uint64_t)clusters * bpc) / 1024ULL);
  }
  return lfs::freeKb();
}

const char* storeName() {
  if (!storeReady) return "zadne";
  return useSd ? "microSD" : "LittleFS";
}

void storeEnd() {
  if (!storeReady) return;
  if (useSd) ink.sdCardSleep();
  else       lfs::end();
  storeReady = false;
}

bool storeExists(const char* path) {
  if (!storeBegin()) return false;
  return useSd ? ink.getSdFat().exists(path) : lfs::exists(path);
}

size_t storeSize(const char* path) {
  if (!storeBegin()) return 0;
  if (useSd) {
    SdFile f;
    if (!f.open(path, O_RDONLY)) return 0;
    size_t s = (size_t)f.fileSize();
    f.close();
    return s;
  }
  return lfs::size(path);
}

bool storeRemove(const char* path) {
  if (!storeBegin()) return false;
  return useSd ? ink.getSdFat().remove(path) : lfs::remove(path);
}

bool storeRename(const char* from, const char* to) {
  if (!storeBegin()) return false;
  return useSd ? ink.getSdFat().rename(from, to) : lfs::rename(from, to);
}

bool storeMkdir(const char* path) {
  if (!storeReady) return false;
  if (useSd) {
    if (ink.getSdFat().exists(path)) return true;
    return ink.getSdFat().mkdir(path);
  }
  return lfs::mkdir(path);
}

// A wrapper over two incompatible file types. The caller only ever sees an
// opaque pointer and never learns about the difference.
namespace {
struct SkyFile {
  bool   sd;
  SdFile s;
  void*  l;   // pointer from lfs::, the File type from FS.h must not appear
};
}  // namespace

StoreFile storeOpenRead(const char* path) {
  if (!storeBegin()) return nullptr;
  SkyFile* f = new SkyFile();
  f->sd = useSd;
  f->l = nullptr;
  if (useSd) {
    if (!f->s.open(path, O_RDONLY)) { delete f; return nullptr; }
  } else {
    f->l = lfs::openRead(path);
    if (!f->l) { delete f; return nullptr; }
  }
  return (StoreFile)f;
}

StoreFile storeOpenWrite(const char* path) {
  if (!storeBegin()) return nullptr;
  SkyFile* f = new SkyFile();
  f->sd = useSd;
  f->l = nullptr;
  if (useSd) {
    if (!f->s.open(path, O_WRONLY | O_CREAT | O_TRUNC)) { delete f; return nullptr; }
  } else {
    f->l = lfs::openWrite(path);
    if (!f->l) { delete f; return nullptr; }
  }
  return (StoreFile)f;
}

size_t storeRead(StoreFile h, void* buf, size_t len) {
  if (!h) return 0;
  SkyFile* f = (SkyFile*)h;
  if (f->sd) { int n = f->s.read(buf, len); return n < 0 ? 0 : (size_t)n; }
  return lfs::read(f->l, buf, len);
}

size_t storeWrite(StoreFile h, const void* buf, size_t len) {
  if (!h) return 0;
  SkyFile* f = (SkyFile*)h;
  return f->sd ? f->s.write(buf, len) : lfs::write(f->l, buf, len);
}

bool storeSeek(StoreFile h, uint32_t pos) {
  if (!h) return false;
  SkyFile* f = (SkyFile*)h;
  return f->sd ? f->s.seekSet(pos) : lfs::seek(f->l, pos);
}

bool storeReadLine(StoreFile h, char* buf, size_t maxLen) {
  if (!h || maxLen < 2) return false;
  SkyFile* f = (SkyFile*)h;
  if (f->sd) return f->s.fgets(buf, (int16_t)maxLen) > 0;
  return lfs::readLine(f->l, buf, maxLen);
}

void storeClose(StoreFile h) {
  if (!h) return;
  SkyFile* f = (SkyFile*)h;
  if (f->sd) f->s.close();
  else       lfs::close(f->l);
  delete f;
}

// ---------------------------------------------------------------- settings
// Config keeps its own Preferences instance; these are for odds and ends.

bool cfgGet(const char*, void*, size_t) { return false; }
bool cfgPut(const char*, const void*, size_t) { return false; }
bool cfgErase(const char*) { return false; }

// ---------------------------------------------------------------- power

float batteryVolts() { return ink.readBattery(); }

bool usbPowered() {
  // The charger status line is not brought out, so this is inferred from
  // the voltage.
  float v = ink.readBattery();
  return v > 4.25f;
}

// ---------------------------------------------------------------- network

bool netConnect(const char* ssid, const char* pass, uint32_t timeoutMs) {
  if (!ssid || !ssid[0]) return false;
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, pass);
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED) {
    if (millis() - t0 > timeoutMs) { WiFi.disconnect(true); WiFi.mode(WIFI_OFF); return false; }
    delay(100);
  }
  return true;
}

void netDisconnect() {
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
}

bool netConnected() { return WiFi.status() == WL_CONNECTED; }
int8_t netRssi()    { return (int8_t)WiFi.RSSI(); }

namespace {

// The response must not be read straight from getStreamPtr(). That returns
// raw socket data, so with Transfer-Encoding: chunked the stream also carries
// the chunk size frames in hexadecimal. A JSON parser then reads "3ba" as a
// number, declares the document complete and returns Ok with nothing in it.
// A CSV parser survives it, but mangled rows slip through.
//
// HTTPClient::writeToStream() does the decoding and handles Content-Length
// and chunked alike. It needs a Stream to write into, which is exactly what
// this class is: whatever arrives is handed on to the callback.
class ChunkSink : public Stream {
  public:
    ChunkSink(ChunkFn cb, void* ctx) : cb_(cb), ctx_(ctx), stop_(false) {}

    using Print::write;

    size_t write(uint8_t b) override { return write(&b, 1); }

    size_t write(const uint8_t* data, size_t n) override {
      // Once the caller has had enough, the rest is swallowed. Returning a
      // short count would make writeToStream treat it as a write error and
      // throw away what has already been processed.
      if (!stop_ && cb_ && !cb_(data, n, ctx_)) stop_ = true;
      return n;
    }

    int  available() override { return 0; }
    int  read() override      { return -1; }
    int  peek() override      { return -1; }
    void flush() override     {}

    bool stopped() const { return stop_; }

  private:
    ChunkFn cb_;
    void*   ctx_;
    bool    stop_;
};

}  // namespace

int netGet(const char* url, ChunkFn cb, void* ctx, uint32_t timeoutMs) {
  if (!netConnected()) return -1;

  WiFiClientSecure secure;
  // No root certificates are kept here. The data is public, and the worst a
  // spoofed server could do is show the wrong passes.
  secure.setInsecure();

  HTTPClient http;
  http.setTimeout(timeoutMs);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  if (!http.begin(secure, url)) return -1;

  int code = http.GET();
  if (code != 200) { http.end(); return code; }

  ChunkSink sink(cb, ctx);
  int written = http.writeToStream(&sink);
  http.end();

  // A negative result means a transfer error, but when the caller stopped on
  // its own that is fine and whatever it processed still stands.
  if (written < 0 && !sink.stopped()) return -1;
  return 200;
}

bool netSyncTime(const char* ntp1, const char* ntp2, const char* ntp3,
                 uint32_t timeoutMs) {
  if (!netConnected()) return false;

  // configTime() overwrites the TZ environment variable from its arguments.
  // Without restoring it the display would stay on UTC and read an hour or
  // two low. Save it before the call and put it back straight after.
  char savedTz[48] = {0};
  const char* tz = getenv("TZ");
  if (tz) strncpy(savedTz, tz, sizeof(savedTz) - 1);

  // The system clock is knocked down to zero so that a new value can only
  // have come from NTP. Without this the loop below would succeed instantly
  // with the stale time left by a previous sync.
  struct timeval zero = {0, 0};
  settimeofday(&zero, nullptr);

  configTime(0, 0, ntp1, ntp2, ntp3);

  bool ok = false;
  uint32_t t0 = millis();
  while (millis() - t0 < timeoutMs) {
    time_t now = time(nullptr);
    if (now > 1735689600) { timeSet(now); ok = true; break; }
    delay(100);
  }

  if (savedTz[0]) applyTimezone(savedTz);
  return ok;
}

namespace {

// "Tue, 01 Sep 2026 09:12:34 GMT"
bool parseHttpDate(const char* s, time_t& out) {
  static const char* MON = "JanFebMarAprMayJunJulAugSepOctNovDec";
  char mon[4] = {0};
  int d, y, H, M, S;
  const char* comma = strchr(s, ',');
  if (comma) s = comma + 1;
  while (*s == ' ') s++;
  if (sscanf(s, "%d %3s %d %d:%d:%d", &d, mon, &y, &H, &M, &S) != 6) return false;

  const char* p = strstr(MON, mon);
  if (!p) return false;

  struct tm t = {};
  t.tm_year = y - 1900;
  t.tm_mon  = (int)((p - MON) / 3);
  t.tm_mday = d;
  t.tm_hour = H; t.tm_min = M; t.tm_sec = S;
  out = astro::tmToUtc(t);
  return out > 1735689600;
}

}  // namespace

bool netSyncTimeHttp(const char* url, uint32_t timeoutMs) {
  if (!netConnected()) return false;

  WiFiClientSecure secure;
  secure.setInsecure();
  secure.setTimeout(timeoutMs / 1000);

  HTTPClient http;
  http.setTimeout(timeoutMs);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  if (!http.begin(secure, url)) return false;

  const char* want[] = {"Date"};
  http.collectHeaders(want, 1);

  // HEAD is enough; the body is of no interest and skipping it saves time
  // and power.
  int code = http.sendRequest("HEAD");
  String d = http.header("Date");
  http.end();

  if (code <= 0 || d.length() < 20) return false;

  time_t t;
  if (!parseHttpDate(d.c_str(), t)) return false;
  timeSet(t);
  return true;
}

bool apStart(const char*, const char*) { return false; }   // see portal.cpp
void apLoop() {}
void apStop() {}
bool apClientConnected() { return false; }

// ---------------------------------------------------------------- system

void sysRestart() { ESP.restart(); }
uint32_t sysFreeHeap()  { return ESP.getFreeHeap(); }
uint32_t sysFreePsram() { return ESP.getFreePsram(); }
const char* sysBoardName() { return FW_BOARD; }

// The wake-up reason has to be read before anything else touches a
// peripheral. Called first thing from setup().
void boardEarlyInit() {
  // The serial port goes through a CH340C, so there is no USB CDC to wait
  // for. The short delay only lets the monitor catch the first lines.
  Serial.begin(115200);
  delay(120);

  pinMode(PIN_WAKE, INPUT);
  pinMode(PIN_RTCINT, INPUT);

  switch (esp_sleep_get_wakeup_cause()) {
    case ESP_SLEEP_WAKEUP_EXT0:  wake = WAKE_BUTTON; break;
    case ESP_SLEEP_WAKEUP_TIMER: wake = WAKE_TIMER;  break;
    default:                     wake = WAKE_POWERON; break;
  }
}

// The target we meant to wake up at. Zero means there is none.
time_t boardTargetWake() { return targetWake; }

}  // namespace hal

#endif  // BOARD_INKPLATE5V2
