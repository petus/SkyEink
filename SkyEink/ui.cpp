// ui.cpp

#include <Arduino.h>
#include <string.h>
#include <math.h>
#include "ui.h"
#include "i18n.h"
#include "runtime.h"
#include "config.h"
#include "version.h"

// Accents. At 0, accented characters are folded to the plain base letter,
// which is legible and simple. At 1, the acute, caron or ring is drawn above
// the letter from primitives.
#ifndef UI_DRAW_ACCENTS
#define UI_DRAW_ACCENTS 0
#endif

namespace ui {

namespace {

enum Accent : uint8_t { ACC_NONE = 0, ACC_ACUTE, ACC_CARON, ACC_RING };

// Decomposition of a UTF-8 character into a base ASCII letter and a mark.
// Covers the Czech alphabet; anything else passes through unchanged.
struct Fold { uint16_t cp; char base; uint8_t acc; };

const Fold FOLD[] = {
  {0x00E1,'a',ACC_ACUTE},{0x00C1,'A',ACC_ACUTE},
  {0x010D,'c',ACC_CARON},{0x010C,'C',ACC_CARON},
  {0x010F,'d',ACC_CARON},{0x010E,'D',ACC_CARON},
  {0x00E9,'e',ACC_ACUTE},{0x00C9,'E',ACC_ACUTE},
  {0x011B,'e',ACC_CARON},{0x011A,'E',ACC_CARON},
  {0x00ED,'i',ACC_ACUTE},{0x00CD,'I',ACC_ACUTE},
  {0x0148,'n',ACC_CARON},{0x0147,'N',ACC_CARON},
  {0x00F3,'o',ACC_ACUTE},{0x00D3,'O',ACC_ACUTE},
  {0x0159,'r',ACC_CARON},{0x0158,'R',ACC_CARON},
  {0x0161,'s',ACC_CARON},{0x0160,'S',ACC_CARON},
  {0x0165,'t',ACC_CARON},{0x0164,'T',ACC_CARON},
  {0x00FA,'u',ACC_ACUTE},{0x00DA,'U',ACC_ACUTE},
  {0x016F,'u',ACC_RING },{0x016E,'U',ACC_RING },
  {0x00FD,'y',ACC_ACUTE},{0x00DD,'Y',ACC_ACUTE},
  {0x017E,'z',ACC_CARON},{0x017D,'Z',ACC_CARON},
  {0x00B0,'\0',ACC_NONE},  // degree sign, drawn separately
};

// Punctuation the fonts lack. Without this it came out as question marks.
struct Sub { uint16_t cp; char to; };
const Sub SUBST[] = {
  {0x00A0,' '},   // non-breaking space
  {0x2013,'-'},   // en dash
  {0x2014,'-'},
  {0x2212,'-'},   // minus sign
  {0x201E,'"'}, {0x201C,'"'}, {0x201D,'"'},
  {0x2018,'\''}, {0x2019,'\''},
  {0x2026,'.'},   // ellipsis
};

// Decodes one character. Returns how many bytes it consumed.
uint8_t decode(const char* s, uint16_t& cp) {
  uint8_t c = (uint8_t)s[0];
  if (c < 0x80) { cp = c; return 1; }
  if ((c & 0xE0) == 0xC0 && s[1]) {
    cp = (uint16_t)(((c & 0x1F) << 6) | (s[1] & 0x3F)); return 2;
  }
  if ((c & 0xF0) == 0xE0 && s[1] && s[2]) {
    cp = (uint16_t)(((c & 0x0F) << 12) | ((s[1] & 0x3F) << 6) | (s[2] & 0x3F));
    return 3;
  }
  cp = '?';
  return 1;
}

bool fold(uint16_t cp, char& base, uint8_t& acc) {
  for (const Fold& f : FOLD)
    if (f.cp == cp) { base = f.base; acc = f.acc; return true; }
  return false;
}

#if UI_DRAW_ACCENTS
// The mark above a letter. Its size follows the font height so it works on
// a small panel as well as a large one.
void drawAccent(int16_t x, int16_t baseY, int16_t chW, uint8_t acc, hal::Ink c) {
  const int16_t h = hal::textAscent();
  const int16_t top = (int16_t)(baseY - h - h / 10);
  const int16_t w = (int16_t)(chW / 3 + 1);
  const int16_t cx = (int16_t)(x + chW / 2);
  const int16_t th = (int16_t)(h / 8 < 1 ? 1 : h / 8);

  switch (acc) {
    case ACC_ACUTE:
      for (int16_t i = 0; i < th; i++)
        hal::drawLine((int16_t)(cx - w / 2), (int16_t)(top + w / 2 + i),
                      (int16_t)(cx + w / 2), (int16_t)(top + i), c);
      break;
    case ACC_CARON:
      for (int16_t i = 0; i < th; i++) {
        hal::drawLine((int16_t)(cx - w / 2), (int16_t)(top + i),
                      cx, (int16_t)(top + w / 2 + i), c);
        hal::drawLine(cx, (int16_t)(top + w / 2 + i),
                      (int16_t)(cx + w / 2), (int16_t)(top + i), c);
      }
      break;
    case ACC_RING:
      hal::drawCircle(cx, (int16_t)(top + w / 2), (int16_t)(w / 2), c);
      break;
    default: break;
  }
}
#endif  // UI_DRAW_ACCENTS

// The separator between values. The fonts lack it, so it is drawn as a dot.
void drawMiddot(int16_t x, int16_t baseY, hal::Ink c) {
  const int16_t h = hal::textAscent();
  const int16_t r = (int16_t)(h / 10 < 1 ? 1 : h / 10);
  hal::fillCircle((int16_t)(x + r + 1), (int16_t)(baseY - h / 3), r, c);
}

int16_t middotW() {
  const int16_t h = hal::textAscent();
  const int16_t r = (int16_t)(h / 10 < 1 ? 1 : h / 10);
  return (int16_t)(2 * r + 4);
}

void drawDegree(int16_t x, int16_t baseY, hal::Ink c) {
  const int16_t h = hal::textAscent();
  const int16_t r = (int16_t)(h / 6 < 2 ? 2 : h / 6);
  hal::drawCircle((int16_t)(x + r), (int16_t)(baseY - h + r), r, c);
}

int16_t degreeW() {
  const int16_t h = hal::textAscent();
  const int16_t r = (int16_t)(h / 6 < 2 ? 2 : h / 6);
  return (int16_t)(2 * r + 2);
}

// One place that both measures and draws the text. As two functions they
// jednou se rozejdou.
int16_t run(int16_t x, int16_t y, const char* s, hal::Ink c, bool draw) {
  int16_t w = 0;
  while (*s) {
    uint16_t cp;
    uint8_t used = decode(s, cp);
    s += used;

    if (cp == 0x00B0) {
      if (draw) drawDegree((int16_t)(x + w), y, c);
      w = (int16_t)(w + degreeW());
      continue;
    }
    if (cp == 0x00B7 || cp == 0x2022) {
      if (draw) drawMiddot((int16_t)(x + w), y, c);
      w = (int16_t)(w + middotW());
      continue;
    }
    if (cp >= 0x80) {
      for (const Sub& sb : SUBST)
        if (sb.cp == cp) { cp = (uint16_t)sb.to; break; }
    }

    char base; uint8_t acc = ACC_NONE;
    if (cp < 0x80) { base = (char)cp; }
    else if (!fold(cp, base, acc)) { base = '?'; }

    char one[2] = {base, 0};
    int16_t cw = hal::textWidth(one);
    if (draw) {
      hal::drawText((int16_t)(x + w), y, one, hal::ALIGN_LEFT, c);
#if UI_DRAW_ACCENTS
      if (acc != ACC_NONE) drawAccent((int16_t)(x + w), y, cw, acc, c);
#else
      (void)acc;
#endif
    }
    w = (int16_t)(w + cw);
  }
  return w;
}

}  // namespace

int16_t textW(const char* utf8) { return run(0, 0, utf8, hal::INK_BLACK, false); }

int16_t text(int16_t x, int16_t y, const char* utf8, hal::Align a, hal::Ink c) {
  int16_t w = textW(utf8);
  int16_t x0 = x;
  if (a == hal::ALIGN_CENTER) x0 = (int16_t)(x - w / 2);
  else if (a == hal::ALIGN_RIGHT) x0 = (int16_t)(x - w);
  run(x0, y, utf8, c, true);
  return w;
}

void fit(char* buf, size_t bufLen, int16_t maxW) {
  if (textW(buf) <= maxW) return;
  size_t n = strlen(buf);
  while (n > 1) {
    // Never cut in the middle of a multi-byte character.
    n--;
    while (n > 0 && ((uint8_t)buf[n] & 0xC0) == 0x80) n--;
    if (n + 2 >= bufLen) continue;
    buf[n] = '.'; buf[n + 1] = '.'; buf[n + 2] = 0;
    if (textW(buf) <= maxW) return;
    buf[n] = 0;
  }
}

void rule(int16_t y, hal::Ink c) {
  const hal::DispCaps& d = hal::dispCaps();
  const hal::Metrics&  m = hal::metrics();
  hal::fillRect(m.pad, y, (int16_t)(d.w - 2 * m.pad), m.ruleW, c);
}

void shade(const Rect& r, hal::Ink c) {
  hal::fillRect(r.x, r.y, r.w, r.h, c);
}

void bar(const Rect& r, float frac, bool fromTop, hal::Ink fill) {
  if (frac < 0) frac = 0;
  if (frac > 1) frac = 1;
  hal::drawRect(r.x, r.y, r.w, r.h, hal::INK_MID);
  int16_t h = (int16_t)(r.h * frac + 0.5f);
  if (h <= 0) return;
  int16_t y = fromTop ? r.y : (int16_t)(r.bottom() - h);
  hal::fillRect(r.x, y, r.w, h, fill);
}

void fmtClock(char* buf, size_t n, time_t utc) {
  struct tm t;
  localtime_r(&utc, &t);
  snprintf(buf, n, "%02d:%02d", t.tm_hour, t.tm_min);
}

void fmtDate(char* buf, size_t n, time_t utc) {
  struct tm t;
  localtime_r(&utc, &t);
  char dow[8];
  snprintf(dow, sizeof(dow), "%s", Tpart(S_DOW, (uint8_t)(t.tm_wday % 7)));

  // Czech uses the month number, English its abbreviation. Both the format
  // and the names come from the string table; this only picks the variant.
  if (i18nGet() == LANG_EN) {
    char mon[8];
    snprintf(mon, sizeof(mon), "%s", Tpart(S_MONTHS, (uint8_t)(t.tm_mon % 12)));
    snprintf(buf, n, T(S_DATE_CS), dow, t.tm_mday, mon);
  } else {
    snprintf(buf, n, T(S_DATE_CS), dow, t.tm_mday, t.tm_mon + 1);
  }
}

void fmtAge(char* buf, size_t n, time_t then, time_t now) {
  if (!then) { snprintf(buf, n, T(S_AGE_NEVER)); return; }
  long d = (long)(now - then);
  if (d < 0) d = 0;
  if (d < 3600)        snprintf(buf, n, T(S_AGE_MIN), d / 60);
  else if (d < 86400)  snprintf(buf, n, T(S_AGE_H), d / 3600);
  else                 snprintf(buf, n, T(S_AGE_D), d / 86400);
}

void fmtMag(char* buf, size_t n, float mag) {
  int m10 = (int)lroundf(mag * 10.0f);
  snprintf(buf, n, "%s%d,%d", m10 < 0 ? "-" : "+",
           abs(m10) / 10, abs(m10) % 10);
}

uint8_t battPercent(float v) {
  if (v != v) return 0;                 // NaN, the board cannot measure
  // Discharge curve of a single lithium cell at rest.
  static const float V[] = {3.30f, 3.50f, 3.60f, 3.70f, 3.75f, 3.80f,
                            3.85f, 3.90f, 3.95f, 4.00f, 4.10f, 4.20f};
  static const uint8_t P[] = {0, 8, 18, 35, 46, 56, 65, 73, 81, 88, 95, 100};
  const uint8_t N = sizeof(P);

  if (v <= V[0]) return 0;
  if (v >= V[N - 1]) return 100;
  for (uint8_t i = 1; i < N; i++) {
    if (v <= V[i]) {
      float f = (v - V[i - 1]) / (V[i] - V[i - 1]);
      return (uint8_t)(P[i - 1] + f * (P[i] - P[i - 1]) + 0.5f);
    }
  }
  return 100;
}

const char* compass(int16_t az) {
  while (az < 0) az += 360;
  az %= 360;
  // One sector is 22.5 degrees, so the index is round(az / 22.5). Written in
  // integer arithmetic that is (az * 2 + 22) / 45. The earlier version used
  // 90 in the divisor, which is round(az / 45): only eight distinct values
  // into a sixteen entry table, so everything except north came out wrong.
  // True east printed as NE and true west as SE.
  return Tpart(S_COMPASS, (uint8_t)(((az * 2 + 22) / 45) % 16));
}

void drawHeader(const layout::Frame& f, const char* title, time_t now) {
  // Top row: title on the left, the current time on the right in a large
  // font.
  hal::setFont(hal::FONT_XL);
  const int16_t topBase = (int16_t)(f.header.y + hal::textAscent());
  char c[8];
  fmtClock(c, sizeof(c), now);
  const int16_t clockW = text(f.header.right(), topBase, c,
                              hal::ALIGN_RIGHT, hal::INK_BLACK);
  const int16_t topH = hal::textHeight();

  // The title only gets what is left of the clock. Without this, a long
  // satellite name such as "STARLINK-31842 x12" ran into the time.
  hal::setFont(hal::FONT_L);
  char t[40];
  snprintf(t, sizeof(t), "%s", title ? title : "");
  fit(t, sizeof(t), (int16_t)(f.header.w - clockW - hal::metrics().gap * 2));
  text(f.header.x, topBase, t, hal::ALIGN_LEFT, hal::INK_BLACK);

  // Bottom row: date on the left, refresh times on the right.
  hal::setFont(hal::FONT_S);
  const int16_t botBase = (int16_t)(f.header.y + topH + hal::textAscent());

  time_t got = rt.lastNetOkAt ? rt.lastNetOkAt : rt.cloudFetchedAt;
  char line[64];
  if (got && rt.nextWakeAt > now) {
    char a[8], b[8];
    fmtClock(a, sizeof(a), got);
    fmtClock(b, sizeof(b), rt.nextWakeAt);
    snprintf(line, sizeof(line), T(S_HDR_DATA), a, b);
  } else if (got) {
    char a[8];
    fmtClock(a, sizeof(a), got);
    snprintf(line, sizeof(line), T(S_HDR_DATA1), a);
  } else {
    snprintf(line, sizeof(line), T(S_HDR_NODATA));
  }
  const int16_t lineW = text(f.header.right(), botBase, line,
                             hal::ALIGN_RIGHT, hal::INK_DARK);

  // The date goes last, into whatever width is left.
  char d[24];
  fmtDate(d, sizeof(d), now);
  fit(d, sizeof(d), (int16_t)(f.header.w - lineW - hal::metrics().gap * 2));
  text(f.header.x, botBase, d, hal::ALIGN_LEFT, hal::INK_DARK);

  rule(f.ruleHeaderY, hal::INK_BLACK);
}

void drawStatus(const layout::Frame& f, time_t now) {
  rule(f.ruleStatusY, hal::INK_BLACK);
  hal::setFont(hal::FONT_XS);
  const int16_t y = (int16_t)(f.status.y + hal::textAscent());

  // On the left, the age of whatever goes stale fastest.
  char gp[24], wx[24], line[96];
  fmtAge(gp, sizeof(gp), rt.gpFetchedAt, now);
  fmtAge(wx, sizeof(wx), rt.cloudFetchedAt, now);
  snprintf(line, sizeof(line), T(S_BAR_AGES), gp, wx, (unsigned)rt.gpCount);
  text(f.status.x, y, line, hal::ALIGN_LEFT, hal::INK_DARK);

  // On the right, the version and the battery.
  float v = hal::batteryVolts();
  char right[80];
  if (v == v) {  // not NaN
    if (hal::usbPowered())
      snprintf(right, sizeof(right), T(S_BAR_CHARGING), FW_NAME, FW_VERSION, v);
    else
      snprintf(right, sizeof(right), T(S_BAR_BATT), FW_NAME, FW_VERSION,
               battPercent(v), v);
  } else {
    snprintf(right, sizeof(right), "%s v%s", FW_NAME, FW_VERSION);
  }
  text(f.status.right(), y, right, hal::ALIGN_RIGHT, hal::INK_DARK);
}

}  // namespace ui
