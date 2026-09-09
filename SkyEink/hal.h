// hal.h - the only interface between the application and the hardware.
//
// RULE: no other file in this project may include Inkplate.h, GxEPD2,
// Adafruit_GFX, esp_sleep.h, SD.h, Wire.h or anything else board specific.
// The application knows this header and nothing more. The implementation
// lives in board_*.cpp.
//
// Supported targets are picked with a single define in board_config.h:
//   BOARD_INKPLATE5V2   ESP32-WROVER, 1280x720, 8 greys, PCF85063A, microSD
//   BOARD_WAVESHARE75   ESP32-S3, 800x480, 1 bit, no RTC, LittleFS

#pragma once
#include "board_config.h"
#include <stdint.h>
#include <stddef.h>
#include <time.h>

namespace hal {

// ---------------------------------------------------------------- display

// Semantic inks. The application never deals with level numbers or dither
// patterns. A board with eight greys maps them to real levels, a one bit
// panel maps them to a dither pattern.
enum Ink : uint8_t {
  INK_WHITE = 0,
  INK_LIGHT,     // 25 % coverage
  INK_MID,       // 50 % coverage
  INK_DARK,      // 75 % coverage
  INK_BLACK
};

// Semantic font sizes. The board works out the actual pixel size from the
// panel height, so the same layout works on 720 and on 480 lines.
enum FontSize : uint8_t {
  FONT_XS = 0,   // axis labels, units
  FONT_S,        // secondary text, data age
  FONT_M,        // tables, body text
  FONT_L,        // band headings
  FONT_XL        // headline value, countdown
};

enum Align : uint8_t { ALIGN_LEFT = 0, ALIGN_CENTER, ALIGN_RIGHT };

// Refresh mode. On Inkplate, eight greys and partial refresh are mutually
// exclusive, so this has to be an explicit application choice rather than a
// hidden board property.
enum DrawMode : uint8_t {
  MODE_MONO = 0,   // 1 bit, partial refresh available
  MODE_GRAY        // more levels, full refresh only
};

struct DispCaps {
  int16_t  w;             // width in px after rotation
  int16_t  h;             // height in px after rotation
  uint8_t  grayLevels;    // 2, 4 or 8
  bool     partial;       // supports partial refresh in MODE_MONO
  // Holds the image without power. This, not the resolution, is the real
  // dividing line between e-paper and LCD. The application uses it to decide
  // whether it may sleep or has to stay awake to keep the image.
  bool     persistent;
  uint16_t fullRefreshMs; // rough duration of a full refresh
  uint16_t partRefreshMs; // rough duration of a partial refresh
};

const DispCaps& dispCaps();

bool dispBegin();
void dispEnd();                       // puts the panel to sleep
bool dispSetMode(DrawMode m);         // false = board cannot do that mode
DrawMode dispMode();
void dispClear(Ink bg = INK_WHITE);
void dispFlushFull();
void dispFlushPartial();              // behaves as full in MODE_GRAY

void drawPixel(int16_t x, int16_t y, Ink c);
void drawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, Ink c);
void drawHLine(int16_t x, int16_t y, int16_t len, Ink c);
void drawVLine(int16_t x, int16_t y, int16_t len, Ink c);
void drawRect(int16_t x, int16_t y, int16_t w, int16_t h, Ink c);
void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, Ink c);
void drawCircle(int16_t cx, int16_t cy, int16_t r, Ink c);
void fillCircle(int16_t cx, int16_t cy, int16_t r, Ink c);
void drawArc(int16_t cx, int16_t cy, int16_t r,
             float a0deg, float a1deg, Ink c);   // for the azimuth arc

// Dashed line. Boards do not offer this consistently, hence the HAL.
void drawDashed(int16_t x0, int16_t y0, int16_t x1, int16_t y1,
                uint8_t on, uint8_t off, Ink c);

// QR code. The generator ships with ESP-IDF; another board may do it
// differently, so this belongs here rather than in the screen code. The code
// is drawn centred in a square of the given side, on white. False on error.
bool drawQr(int16_t x, int16_t y, int16_t side, const char* text);

void setFont(FontSize f);
void drawText(int16_t x, int16_t y, const char* utf8, Align a, Ink c);
int16_t textWidth(const char* utf8);
int16_t textHeight();                 // line height of the current font
int16_t textAscent();                 // for baseline alignment

// Characters the fonts usually lack (° µ ³) are drawn from primitives by the
// implementation. The application just writes normal UTF-8.

// ---------------------------------------------------------------- layout

// Guide values derived from the panel size. The layout is computed from
// these, never from pixel constants, otherwise a port to another panel
// falls apart.
struct Metrics {
  int16_t pad;        // outer margin
  int16_t gap;        // gap between elements
  int16_t rowH;       // table row height
  int16_t ruleW;      // divider thickness
  float   scale;      // 1.0 at 720 lines, proportional otherwise
};
const Metrics& metrics();

// ---------------------------------------------------------------- time

// True when the board has a crystal RTC with a backup cell. The application
// uses it only to decide how often to sync over NTP.
// Sets the time zone from a POSIX TZ string and returns the resulting offset
// from UTC in seconds. The return value is there to confirm the string was
// actually understood.
long  applyTimezone(const char* posixTz);
const char* timezoneName();

bool  rtcPresent();
bool  timeValid();
time_t timeNow();                     // UTC
void  timeSet(time_t utc);            // also writes the RTC when present
uint32_t timeErrorEstimateSec();      // estimated drift since the last sync

// ---------------------------------------------------------------- sleep

enum WakeReason : uint8_t {
  WAKE_POWERON = 0,
  WAKE_TIMER,
  WAKE_BUTTON,
  WAKE_RTC_ALARM,
  WAKE_UNKNOWN
};

WakeReason wakeReason();

// Suspends until the given time. On a board with a persistent display that
// means deep sleep, split into shorter chunks with drift corrected against
// the RTC. On a board that has to keep refreshing, it means waiting awake.
// The application only supplies the target time. Does not return; after the
// wake-up execution starts again from the top.
[[noreturn]] void idleUntil(time_t utcWake);

// ---------------------------------------------------------------- buttons

enum Button : uint8_t { BTN_A = 0, BTN_B, BTN_COUNT };

// Bit mask of the buttons the board actually has. The application has to
// behave sensibly even when there are none.
uint8_t buttonsPresent();
bool buttonCanWake(Button b);         // is the pin in the RTC domain
bool buttonDown(Button b);
// How long the button was held, in milliseconds. On boards that cannot wake
// from a button, this is the hold time after a reset.
uint32_t buttonHeldMs(Button b);

// ---------------------------------------------------------------- storage

// A minimal file interface. microSD on Inkplate, LittleFS elsewhere.
// Streaming writes are needed to filter orbital elements on the fly.
bool storeBegin();
// "microSD", "LittleFS" or "none". For the log only.
const char* storeName();

// Free space in KiB, -1 when the board cannot tell.
// CAUTION: on a FAT32 card the first call walks the whole allocation table
// and takes seconds on a large card. Call it only where that does not matter,
// meaning the status screen or an error report, never on a routine wake-up.
int32_t storeFreeKb();
void storeEnd();
bool storeExists(const char* path);
size_t storeSize(const char* path);
bool storeRemove(const char* path);
bool storeRename(const char* from, const char* to);
bool storeMkdir(const char* path);

typedef void* StoreFile;
StoreFile storeOpenRead(const char* path);
StoreFile storeOpenWrite(const char* path);
size_t storeRead(StoreFile f, void* buf, size_t len);
size_t storeWrite(StoreFile f, const void* buf, size_t len);
bool storeSeek(StoreFile f, uint32_t pos);
bool storeReadLine(StoreFile f, char* buf, size_t maxLen);
void storeClose(StoreFile f);

// ---------------------------------------------------------------- settings

// Persistent settings outside the file system; survives a card swap.
bool cfgGet(const char* key, void* buf, size_t len);
bool cfgPut(const char* key, const void* buf, size_t len);
bool cfgErase(const char* key);

// ---------------------------------------------------------------- power

// NaN when the board cannot measure it. The application then disables the
// voltage based safeguards.
float batteryVolts();
bool  usbPowered();

// ---------------------------------------------------------------- network

bool netConnect(const char* ssid, const char* pass, uint32_t timeoutMs);
void netDisconnect();
bool netConnected();
int8_t netRssi();

// Chunked download. The callback may return false to stop early, which the
// element filter uses so it does not have to take the whole catalogue.
typedef bool (*ChunkFn)(const uint8_t* data, size_t len, void* ctx);
int netGet(const char* url, ChunkFn cb, void* ctx, uint32_t timeoutMs);

// Time sync. NTP runs over UDP port 123, which some ISPs block. Next to it
// there is a route through the Date header of an HTTPS response, which uses
// port 443 and gets through wherever the data download does. The Date header
// is accurate to a second, which is enough for pass prediction.
bool netSyncTime(const char* ntp1, const char* ntp2, const char* ntp3,
                 uint32_t timeoutMs);
bool netSyncTimeHttp(const char* url, uint32_t timeoutMs);

// Configuration access point. True while it is running.
bool apStart(const char* ssid, const char* pass);
void apLoop();
void apStop();
bool apClientConnected();

// ---------------------------------------------------------------- system

void   sysRestart();
uint32_t sysFreeHeap();
uint32_t sysFreePsram();
const char* sysBoardName();

// Called first thing in setup(), before dispBegin(). It records the wake-up
// reason before touching peripherals can overwrite it.
void boardEarlyInit();

// The time the device meant to wake up at. Zero when there is none.
time_t boardTargetWake();

}  // namespace hal
