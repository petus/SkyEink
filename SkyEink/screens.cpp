// screens.cpp
//
// Every coordinate comes from layout::. Not one hard coded pixel value may
// appear in this file, otherwise a port to another panel
// rozsype.

#include <Arduino.h>
#include <string.h>
#include <math.h>
#include "screens.h"
#include "ui.h"
#include "i18n.h"
#include "layout.h"
#include "hal.h"
#include "runtime.h"
#include "config.h"
#include "astro.h"
#include "weather.h"
#include "passes.h"
#include "gp.h"
#include "version.h"

namespace screens {

using layout::Rect;
using namespace hal;

namespace {

// Below this solar elevation we call it dusk, for labelling purposes.
const double TWILIGHT = -6.0;

// The precipitation caption. Not much fits in a column, and exactly how much
// depends on the panel and the font. Variants are tried from the fullest
// down, and the first one that fits the measured width wins. An empty string
// means nothing is drawn, because a zero in every hour is just clutter.
bool precipLabel(char* buf, size_t n, uint8_t mm10, uint8_t prob, int16_t maxW) {
  if (mm10 == 0 && prob < 10) { buf[0] = 0; return false; }

  const int whole = mm10 / 10, frac = mm10 % 10;

  if (mm10 > 0) {
    snprintf(buf, n, "%d,%d mm · %u %%", whole, frac, (unsigned)prob);
    if (ui::textW(buf) <= maxW) return true;

    snprintf(buf, n, "%d,%d · %u %%", whole, frac, (unsigned)prob);
    if (ui::textW(buf) <= maxW) return true;

    snprintf(buf, n, "%d,%d mm", whole, frac);
    if (ui::textW(buf) <= maxW) return true;

    snprintf(buf, n, "%d,%d", whole, frac);
    return ui::textW(buf) <= maxW;
  }

  // No precipitation expected, but the probability is not negligible.
  snprintf(buf, n, "%u %%", (unsigned)prob);
  return ui::textW(buf) <= maxW;
}

void hourLabel(char* buf, size_t n, time_t utc) {
  struct tm t;
  localtime_r(&utc, &t);
  snprintf(buf, n, "%02d", t.tm_hour);
}

// ---------------------------------------------------------------- weather

void drawCloudBand(const layout::Night& L, time_t from) {
  setFont(FONT_M);
  ui::text(L.cloudLabel.x, (int16_t)(L.cloudLabel.y + textAscent()),
           T(S_CLOUD_BAND), ALIGN_LEFT, INK_BLACK);

  // Summary on the right: how long it stays clear, the temperature range
  // and the highest chance of precipitation.
  if (rt.cloud.hours) {
    int firstBad = -1;
    for (uint8_t i = 0; i < L.cloudCols && i < rt.cloud.hours; i++)
      if (rt.cloud.total[i] > 50) { firstBad = i; break; }

    char s[96];
    int len;
    if (firstBad < 0)       len = snprintf(s, sizeof(s), T(S_CLEAR_NIGHT));
    else if (firstBad == 0) len = snprintf(s, sizeof(s), T(S_OVERCAST));
    else {
      char hh[8];
      hourLabel(hh, sizeof(hh), from + (time_t)firstBad * 3600);
      len = snprintf(s, sizeof(s), T(S_CLEAR_UNTIL), hh);
    }

    int8_t lo = 0, hi = 0;
    if (weather::tempRange(lo, hi) && len > 0 && len < (int)sizeof(s))
      len += snprintf(s + len, sizeof(s) - len, T(S_TEMP_RANGE), lo, hi);

    float mm = weather::totalMm();
    uint8_t mr = weather::maxRain();
    if (len > 0 && len < (int)sizeof(s)) {
      if (mm >= 0.05f)
        snprintf(s + len, sizeof(s) - len, T(S_PRECIP_SUM),
                 (double)mm, mr);
      else if (mr >= 10)
        snprintf(s + len, sizeof(s) - len, T(S_PRECIP_CHANCE), mr);
    }

    setFont(FONT_S);
    ui::text(L.cloudLabel.right(), (int16_t)(L.cloudLabel.y + textAscent()),
             s, ALIGN_RIGHT, INK_DARK);
  }

  for (uint8_t i = 0; i < L.cloudCols; i++) {
    Rect b;
    b.x = (int16_t)(L.cloudBars.x + i * (L.cloudBarW + L.cloudBarGap));
    b.y = L.cloudBars.y;
    b.w = L.cloudBarW;
    b.h = L.cloudBars.h;

    time_t slotT = from + (time_t)i * 3600;
    int idx = weather::slotFor(slotT);

    drawRect(b.x, b.y, b.w, b.h, INK_MID);

    if (idx >= 0) {
      // Cloud fills from the top down, because that is where it comes from.
      // Precipitation fills from the bottom up, so the two cannot be
      // confused.
      Ink fill = (weather::ageHours(slotT) > 12) ? INK_MID : INK_DARK;
      int16_t ch = (int16_t)(b.h * rt.cloud.total[idx] / 100);
      if (ch > 0) fillRect((int16_t)(b.x + 1), (int16_t)(b.y + 1),
                           (int16_t)(b.w - 2), ch, fill);

    }

    const int16_t cx = (int16_t)(b.x + b.w / 2);
    int16_t ry = L.cloudHours.y;

    setFont(FONT_M);
    char hh[8];
    hourLabel(hh, sizeof(hh), slotT);
    ui::text(cx, (int16_t)(ry + textAscent()), hh, ALIGN_CENTER, INK_DARK);
    ry = (int16_t)(ry + L.rowHour);

    if (idx >= 0 && rt.cloud.temp[idx] != -128) {
      setFont(FONT_L);
      char tt[12];
      snprintf(tt, sizeof(tt), "%d°", rt.cloud.temp[idx]);
      ui::text(cx, (int16_t)(ry + textAscent()), tt, ALIGN_CENTER, INK_BLACK);
    }
    ry = (int16_t)(ry + L.rowTemp);

    // A row height of zero means it did not fit the panel and the layout
    // dropped it. The summary in the band heading still carries the value.
    if (idx >= 0 && L.rowPrecip > 0) {
      setFont(FONT_S);
      char pp[24];
      if (precipLabel(pp, sizeof(pp), rt.cloud.mm10[idx], rt.cloud.rain[idx],
                      (int16_t)(b.w - 2)))
        ui::text(cx, (int16_t)(ry + textAscent()), pp, ALIGN_CENTER, INK_BLACK);
    }
  }
}

// ---------------------------------------------------------------- passes

void drawPassTable(const layout::Night& L, time_t now) {
  setFont(FONT_M);
  ui::text(L.tableLabel.x, (int16_t)(L.tableLabel.y + textAscent()),
           T(S_PASSES), ALIGN_LEFT, INK_BLACK);

  static const char* HEAD[layout::COL_COUNT] =
      {T(S_COL_TIME), T(S_COL_OBJ), T(S_COL_DIR),
       T(S_COL_MAX), T(S_COL_MAG), T(S_COL_DUR)};

  setFont(FONT_XS);
  for (uint8_t c = 0; c < layout::COL_COUNT; c++)
    ui::text(L.colX[c], (int16_t)(L.tableHead.y + textAscent()),
             HEAD[c], ALIGN_LEFT, INK_MID);

  if (!rt.passCount) {
    setFont(FONT_M);
    ui::text(L.tableBody.x, (int16_t)(L.tableBody.y + L.rowH),
             rt.gpFetchedAt ? T(S_NO_PASSES)
                            : T(S_NO_ELEMENTS),
             ALIGN_LEFT, INK_DARK);
    return;
  }

  // The brightest pass still to come gets a grey background.
  uint8_t best = 0xFF;
  for (uint8_t i = 0; i < rt.passCount; i++) {
    if (rt.passes[i].tSet < now) continue;
    if (best == 0xFF || rt.passes[i].mag < rt.passes[best].mag) best = i;
  }

  // Passes that are over are skipped. They used to stay in the list greyed
  // out, taking up rows, so the later ones never reached the screen.
  uint8_t upcoming = 0;
  for (uint8_t i = 0; i < rt.passCount; i++)
    if (rt.passes[i].tSet >= now) upcoming++;

  if (!upcoming) {
    setFont(FONT_M);
    ui::text(L.tableBody.x, (int16_t)(L.tableBody.y + L.rowH),
             T(S_NO_MORE), ALIGN_LEFT, INK_DARK);
    return;
  }

  uint8_t shown = 0;
  for (uint8_t i = 0; i < rt.passCount && shown < L.rows; i++) {
    const PassRec& p = rt.passes[i];
    if (p.tSet < now) continue;

    int16_t y = (int16_t)(L.tableBody.y + shown * L.rowH);

    if (i == best)
      fillRect(L.tableBody.x, y, L.tableBody.w, L.rowH, INK_LIGHT);

    // A faint pass is drawn in grey, so it is obvious at a glance that it
    // is not worth going outside for without binoculars.
    Ink ink = (p.mag > 2.0f) ? INK_MID : INK_BLACK;

    setFont(FONT_M);
    int16_t ty = (int16_t)(y + (L.rowH + textAscent()) / 2);

    char buf[32];
    ui::fmtClock(buf, sizeof(buf), p.tRise);
    ui::text(L.colX[layout::COL_TIME], ty, buf, ALIGN_LEFT, ink);

    // Merged passes with differing names, typically a Starlink train, get a
    // count. The same body listed twice in the catalogue gets none, because
    // it is still one object.
    if (p.group > 1 && p.mixed)
      snprintf(buf, sizeof(buf), "%s ×%u", p.name, (unsigned)p.group);
    else
      snprintf(buf, sizeof(buf), "%s", p.name);
    ui::fit(buf, sizeof(buf), (int16_t)(L.colW[layout::COL_NAME] - metrics().gap));
    ui::text(L.colX[layout::COL_NAME], ty, buf, ALIGN_LEFT, ink);

    snprintf(buf, sizeof(buf), T(S_DIR_TO),
             ui::compass(p.azRise), ui::compass(p.azSet));
    ui::text(L.colX[layout::COL_DIR], ty, buf, ALIGN_LEFT, ink);

    snprintf(buf, sizeof(buf), "%d°", p.elMax);
    ui::text(L.colX[layout::COL_MAXEL], ty, buf, ALIGN_LEFT, ink);

    ui::fmtMag(buf, sizeof(buf), p.mag);
    ui::text(L.colX[layout::COL_MAG], ty, buf, ALIGN_LEFT, ink);

    long mins = (long)(p.tSet - p.tRise) / 60;
    snprintf(buf, sizeof(buf), T(S_MIN), mins < 1 ? 1 : mins);
    ui::text(L.colX[layout::COL_DUR], ty, buf, ALIGN_LEFT, ink);

    shown++;
  }

  if (upcoming > shown) {
    setFont(FONT_XS);
    char more[40];
    snprintf(more, sizeof(more), T(S_MORE), (unsigned)(upcoming - shown));
    ui::text(L.tableBody.right(),
             (int16_t)(L.tableBody.bottom() - metrics().gap / 2),
             more, ALIGN_RIGHT, INK_MID);
  }
}

// ---------------------------------------------------------------- planety

void drawMoon(const Rect& panel, int16_t r, const Rect& textArea, time_t t) {
  int16_t cx = (int16_t)(panel.x + panel.w / 2);
  int16_t cy = (int16_t)(panel.y + r);

  bool waxing;
  double frac = astro::moonPhase(t, waxing);

  // The unlit disc first, then the lit crescent or gibbous shape over it.
  fillCircle(cx, cy, r, INK_MID);
  drawCircle(cx, cy, r, INK_BLACK);

  // The terminator is an ellipse with semi-axis r*|1-2f|. Drawing it line by
  // line handles the crescent and the gibbous shape with no special cases.
  double k = 1.0 - 2.0 * frac;         // -1 full, +1 new
  for (int16_t dy = (int16_t)-r; dy <= r; dy++) {
    double yy = (double)dy / (double)r;
    double half = sqrt(1.0 - yy * yy) * r;
    double term = half * k;
    int16_t x0, x1;
    if (waxing) { x0 = (int16_t)lround(term); x1 = (int16_t)lround(half); }
    else        { x0 = (int16_t)lround(-half); x1 = (int16_t)lround(-term); }
    if (x1 > x0)
      drawHLine((int16_t)(cx + x0), (int16_t)(cy + dy), (int16_t)(x1 - x0), INK_WHITE);
  }
  drawCircle(cx, cy, r, INK_BLACK);

  if (textArea.h <= 0) return;
  setFont(FONT_S);

  // One line only. More does not fit under the disc, and a second line used
  // to spill into the status bar.
  const char* phase;
  if (frac < 0.03)      phase = T(S_MOON_NEW);
  else if (frac > 0.97) phase = T(S_MOON_FULL);
  else if (frac < 0.47) phase = waxing ? T(S_MOON_WAX) : T(S_MOON_WANE);
  else if (frac < 0.53) phase = waxing ? T(S_MOON_Q1) : T(S_MOON_Q3);
  else                  phase = waxing ? T(S_MOON_WAX) : T(S_MOON_WANE);

  char s[48];
  snprintf(s, sizeof(s), "%s · %d %%", phase, (int)lround(frac * 100.0));
  ui::fit(s, sizeof(s), textArea.w);

  int16_t ty = (int16_t)(textArea.y + textAscent());
  if (ty <= textArea.bottom())
    ui::text(textArea.right(), ty, s, ALIGN_RIGHT, INK_BLACK);
}

void drawHorizon(const layout::Night& L, time_t t) {
  setFont(FONT_M);
  ui::text(L.horizonLabel.x, (int16_t)(L.horizonLabel.y + textAscent()),
           T(S_PLANETS), ALIGN_LEFT, INK_BLACK);

  const Rect& p = L.horizonPlot;
  // Horizon along the bottom, a dashed line at thirty degrees.
  drawHLine(p.x, (int16_t)(p.bottom() - 1), p.w, INK_BLACK);
  int16_t y30 = (int16_t)(p.bottom() - p.h / 3);
  hal::drawDashed(p.x, y30, p.right(), y30, 4, 5, INK_LIGHT);

  setFont(FONT_XS);
  ui::text((int16_t)(p.x + 4), (int16_t)(y30 - 2), "30°", ALIGN_LEFT, INK_MID);

  // The azimuth axis runs from southeast to northwest, which is where the
  // planets sit after dusk. 135 degrees on the left, 315 on the right.
  const double AZ0 = 100.0, AZ1 = 300.0;
  auto azToX = [&](double az) -> int16_t {
    double f = (az - AZ0) / (AZ1 - AZ0);
    if (f < 0) f = 0;
    if (f > 1) f = 1;
    return (int16_t)(p.x + f * p.w);
  };
  auto elToY = [&](double el) -> int16_t {
    double f = el / 60.0;
    if (f < 0) f = 0;
    if (f > 1) f = 1;
    return (int16_t)(p.bottom() - 1 - f * (p.h - 2));
  };

  // Southeast, south, southwest, west. Indices into the compass list.
  static const struct { double az; uint8_t idx; } TICKS[] = {
    {135, 6}, {180, 8}, {225, 10}, {270, 12}
  };
  for (auto& tk : TICKS) {
    char lbl[8];
    snprintf(lbl, sizeof(lbl), "%s", Tpart(S_COMPASS, tk.idx));
    ui::text(azToX(tk.az), (int16_t)(L.horizonAxis.y + textAscent()),
             lbl, ALIGN_CENTER, INK_MID);
  }

  // Planets. The dot size follows the brightness.
  int drawn = 0;
  for (uint8_t i = 0; i < astro::PL_COUNT; i++) {
    astro::Planet pl = (astro::Planet)i;
    astro::Horiz h = astro::planetAt(pl, t, cfg.lat, cfg.lon);
    if (h.el < 2.0 || h.az < AZ0 || h.az > AZ1) continue;
    double mag = astro::planetMag(pl, t);
    if (mag > 2.5) continue;

    int16_t r = (int16_t)(metrics().scale * (mag < -2.0 ? 10 : (mag < 0.5 ? 8 : 6)));
    int16_t x = azToX(h.az), y = elToY(h.el);
    fillCircle(x, y, r, mag < -2.0 ? INK_BLACK : INK_DARK);

    setFont(FONT_S);
    ui::text(x, (int16_t)(y - r - metrics().gap / 2), astro::planetName(pl),
             ALIGN_CENTER, INK_BLACK);
    drawn++;
  }

  if (!drawn) {
    setFont(FONT_S);
    ui::text(p.cx(), (int16_t)(p.cy()), T(S_NO_PLANET),
             ALIGN_CENTER, INK_MID);
  }
}

// ---------------------------------------------------------------- countdown

void countdownText(char* buf, size_t n, long sec) {
  if (sec <= 0) { snprintf(buf, n, T(S_CD_NOW)); return; }
  if (sec < 90) { snprintf(buf, n, T(S_CD_S), sec); return; }
  if (sec < 3600) {
    // Writing it as 4:30 read like a clock time rather than a duration.
    long m = sec / 60, r = sec % 60;
    if (r >= 5) snprintf(buf, n, T(S_CD_MS), m, r);
    else        snprintf(buf, n, T(S_CD_M), m);
    return;
  }
  long h = sec / 3600, m = (sec % 3600) / 60;
  if (m) snprintf(buf, n, T(S_CD_HM), h, m);
  else   snprintf(buf, n, T(S_CD_H), h);
}

}  // namespace

// ---------------------------------------------------------------- obrazovky

void night(time_t now) {
  dispSetMode(MODE_GRAY);
  dispClear();

  layout::Night L;
  // The weather band starts at dusk, or now if it is already dark.
  time_t start = now - (now % 3600);
  layout::buildNight(9, L);

  ui::drawHeader(L.frame, T(S_TONIGHT), now);
  drawCloudBand(L, start);
  drawPassTable(L, now);
  drawHorizon(L, now);
  drawMoon(L.moon, L.moonR, L.moonText, now);
  ui::drawStatus(L.frame, now);
}

void day(time_t now) {
  dispSetMode(MODE_GRAY);
  dispClear();

  layout::Night L;
  layout::buildNight(9, L);
  ui::drawHeader(L.frame, T(S_SKY_TODAY), now);

  time_t start = now - (now % 3600);
  drawCloudBand(L, start);

  // A summary of the evening instead of the pass table.
  const Rect& t = L.table;
  setFont(FONT_M);
  ui::text(t.x, (int16_t)(t.y + textAscent()), T(S_NEXT_PASS),
           ALIGN_LEFT, INK_BLACK);

  int idx = rtNextPassIndex(now);
  if (idx < 0) {
    setFont(FONT_L);
    ui::text(t.x, (int16_t)(t.y + t.h / 2), T(S_NOTHING),
             ALIGN_LEFT, INK_MID);
  } else {
    const PassRec& p = rt.passes[idx];
    char hh[16], mag[16], line[96];
    ui::fmtClock(hh, sizeof(hh), p.tRise);
    ui::fmtMag(mag, sizeof(mag), p.mag);

    setFont(FONT_XL);
    ui::text(t.x, (int16_t)(t.y + t.h / 2), hh, ALIGN_LEFT, INK_BLACK);
    int16_t x = (int16_t)(t.x + ui::textW("00:00") + metrics().gap * 3);

    setFont(FONT_L);
    ui::text(x, (int16_t)(t.y + t.h / 2), p.name, ALIGN_LEFT, INK_BLACK);

    setFont(FONT_M);
    snprintf(line, sizeof(line), T(S_PASS_SUM),
             mag, p.elMax, ui::compass(p.azRise), ui::compass(p.azSet));
    ui::text(x, (int16_t)(t.y + t.h / 2 + textHeight() + metrics().gap),
             line, ALIGN_LEFT, INK_DARK);
  }

  // Twilight times at the bottom instead of the planets.
  setFont(FONT_M);
  ui::text(L.horizonLabel.x, (int16_t)(L.horizonLabel.y + textAscent()),
           T(S_DUSK_NIGHT), ALIGN_LEFT, INK_BLACK);

  time_t civil = astro::sunCrossing(now, TWILIGHT, true, cfg.lat, cfg.lon);
  time_t astroDark = astro::sunCrossing(now, -18.0, true, cfg.lat, cfg.lon);
  char a[16], b[16], line[128];
  if (civil) ui::fmtClock(a, sizeof(a), civil); else strcpy(a, "--:--");
  if (astroDark) ui::fmtClock(b, sizeof(b), astroDark); else strcpy(b, T(S_NONE));
  snprintf(line, sizeof(line), T(S_DUSK_FMT), a, b);
  setFont(FONT_M);
  ui::text(L.horizonPlot.x, (int16_t)(L.horizonPlot.y + textAscent() + metrics().gap),
           line, ALIGN_LEFT, INK_DARK);

  drawMoon(L.moon, L.moonR, L.moonText, now);
  ui::drawStatus(L.frame, now);
}

void pass(time_t now, uint8_t index, uint8_t alsoIndex) {
  if (index >= rt.passCount) { night(now); return; }
  const PassRec& p = rt.passes[index];

  dispSetMode(MODE_MONO);
  dispClear();

  layout::Pass L;
  layout::buildPass(L);
  ui::drawHeader(L.frame, p.name, now);

  // Polar sky plot. North at the top, east on the right.
  int16_t cx = L.dial.cx(), cy = L.dial.cy(), R = L.dialR;
  drawCircle(cx, cy, R, INK_BLACK);
  drawCircle(cx, cy, (int16_t)(R * 2 / 3), INK_LIGHT);
  drawCircle(cx, cy, (int16_t)(R / 3), INK_LIGHT);

  // The labels go into the strip between the circle and the edge of the
  // square, inside the reserved area. They used to be outside it and ran into
  // the header and the status bar.
  // Indices into the compass list: 0 north, 4 east, 8 south, 12 west.
  setFont(FONT_M);
  char pt[8];
  snprintf(pt, sizeof(pt), "%s", Tpart(S_COMPASS, 0));
  ui::text(cx, (int16_t)(L.dial.y + textAscent()), pt, ALIGN_CENTER, INK_MID);
  snprintf(pt, sizeof(pt), "%s", Tpart(S_COMPASS, 8));
  ui::text(cx, (int16_t)(L.dial.bottom() - 2), pt, ALIGN_CENTER, INK_MID);
  snprintf(pt, sizeof(pt), "%s", Tpart(S_COMPASS, 4));
  ui::text(L.dial.right(), (int16_t)(cy + textAscent() / 2), pt,
           ALIGN_RIGHT, INK_MID);
  snprintf(pt, sizeof(pt), "%s", Tpart(S_COMPASS, 12));
  ui::text(L.dial.x, (int16_t)(cy + textAscent() / 2), pt,
           ALIGN_LEFT, INK_MID);

  auto toXY = [&](double az, double el, int16_t& x, int16_t& y) {
    double rr = R * (90.0 - el) / 90.0;
    double a = az * M_PI / 180.0;
    x = (int16_t)lround(cx + rr * sin(a));
    y = (int16_t)lround(cy - rr * cos(a));
  };

  // The ground track. Points come from the elements, not from the three
  // stored azimuths.
  if (passes::trackBegin(index)) {
    int16_t px = 0, py = 0;
    bool have = false;
    const int STEPS = 40;
    for (int i = 0; i <= STEPS; i++) {
      time_t t = p.tRise + (time_t)((double)(p.tSet - p.tRise) * i / STEPS);
      float az, el;
      if (!passes::trackAt(t, az, el)) { have = false; continue; }
      int16_t x, y;
      toXY(az, el, x, y);
      if (have) drawLine(px, py, x, y, INK_BLACK);
      px = x; py = y; have = true;
    }
    passes::trackEnd();
  } else {
    int16_t x0, y0, x1, y1, x2, y2;
    toXY(p.azRise, 0, x0, y0);
    toXY(p.azMax, p.elMax, x1, y1);
    toXY(p.azSet, 0, x2, y2);
    drawLine(x0, y0, x1, y1, INK_BLACK);
    drawLine(x1, y1, x2, y2, INK_BLACK);
  }

  // The peak marker.
  int16_t mx, my;
  toXY(p.azMax, p.elMax, mx, my);
  fillCircle(mx, my, (int16_t)(4 * metrics().scale + 2), INK_BLACK);

  // Countdown. Once the pass is running, that matters more than the time
  // until it starts.
  setFont(FONT_XL);
  char cd[40];
  if (now >= p.tRise && now <= p.tSet)
    snprintf(cd, sizeof(cd), T(S_IN_PROGRESS));
  else
    countdownText(cd, sizeof(cd), (long)(p.tRise - now));
  ui::fit(cd, sizeof(cd), L.countdown.w);
  ui::text(L.countdown.x, (int16_t)(L.countdown.y + textAscent()), cd,
           ALIGN_LEFT, INK_BLACK);

  // Fakta.
  struct { const char* k; char v[32]; } facts[6];
  uint8_t nf = 0;
  ui::fmtClock(facts[nf].v, sizeof(facts[nf].v), p.tRise); facts[nf].k = T(S_F_RISE); nf++;
  ui::fmtClock(facts[nf].v, sizeof(facts[nf].v), p.tMax);  facts[nf].k = T(S_F_MAX); nf++;
  snprintf(facts[nf].v, sizeof(facts[nf].v), "%d°", p.elMax); facts[nf].k = T(S_F_ELEV); nf++;
  ui::fmtMag(facts[nf].v, sizeof(facts[nf].v), p.mag);     facts[nf].k = T(S_F_MAG); nf++;
  snprintf(facts[nf].v, sizeof(facts[nf].v), T(S_DIR_TO),
           ui::compass(p.azRise), ui::compass(p.azSet)); facts[nf].k = T(S_F_DIR); nf++;
  if (alsoIndex < rt.passCount && alsoIndex != index) {
    char hh[16];
    ui::fmtClock(hh, sizeof(hh), rt.passes[alsoIndex].tRise);
    snprintf(facts[nf].v, sizeof(facts[nf].v), "%s %s", hh, rt.passes[alsoIndex].name);
    facts[nf].k = T(S_F_THEN); nf++;
  }

  for (uint8_t i = 0; i < nf && i < L.factRows; i++) {
    // The value is drawn and measured first, so the label only gets what is
    // left. Otherwise the two would overlap on long values.
    setFont(FONT_L);
    const int16_t y = (int16_t)(L.facts.y + i * L.factRowH + textAscent());
    const int16_t vw = ui::text(L.facts.right(), y, facts[i].v,
                                ALIGN_RIGHT, INK_BLACK);

    setFont(FONT_M);
    char key[24];
    snprintf(key, sizeof(key), "%s", facts[i].k);
    ui::fit(key, sizeof(key), (int16_t)(L.facts.w - vw - metrics().gap * 2));
    ui::text(L.facts.x, y, key, ALIGN_LEFT, INK_MID);
  }

  ui::drawStatus(L.frame, now);
}

void progress(const char* stage, uint8_t pct) {
  layout::Progress L;
  layout::buildProgress(L);

  // Just the block area, not the whole screen, so partial refresh is fast.
  fillRect(L.caption.x, L.caption.y, L.caption.w,
           (int16_t)(L.estimate.bottom() - L.caption.y), INK_WHITE);

  setFont(FONT_L);
  ui::text(L.caption.cx(), (int16_t)(L.caption.y + textAscent()), stage,
           ALIGN_CENTER, INK_BLACK);

  drawRect(L.bar.x, L.bar.y, L.bar.w, L.bar.h, INK_BLACK);
  int16_t w = (int16_t)((L.bar.w - 4) * (pct > 100 ? 100 : pct) / 100);
  if (w > 0)
    fillRect((int16_t)(L.bar.x + 2), (int16_t)(L.bar.y + 2), w,
             (int16_t)(L.bar.h - 4), INK_BLACK);

  setFont(FONT_M);
  char s[24];
  snprintf(s, sizeof(s), "%u %%", pct);
  ui::text(L.estimate.cx(), (int16_t)(L.estimate.y + textAscent()), s,
           ALIGN_CENTER, INK_DARK);
}

void status(time_t now, const char* reason) {
  dispSetMode(MODE_GRAY);
  dispClear();

  layout::Frame f;
  layout::buildFrame(f);
  ui::drawHeader(f, T(S_STATUS), now);

  const Metrics& m = metrics();
  int16_t y = (int16_t)(f.content.y + textAscent());

  if (reason) {
    setFont(FONT_L);
    ui::text(f.content.x, y, reason, ALIGN_LEFT, INK_BLACK);
    y = (int16_t)(y + textHeight() + m.gap);
  }

  setFont(FONT_M);
  const int16_t lh = (int16_t)(textHeight() + m.gap / 2);

  auto row = [&](const char* k, const char* v) {
    ui::text(f.content.x, y, k, ALIGN_LEFT, INK_MID);
    ui::text(f.content.right(), y, v, ALIGN_RIGHT, INK_BLACK);
    y = (int16_t)(y + lh);
  };

  char v[64], a[32];

  ui::fmtAge(a, sizeof(a), rt.gpFetchedAt, now);
  snprintf(v, sizeof(v), T(S_OBJECTS), a, (unsigned)rt.gpCount);
  row(T(S_ST_ELEMENTS), v);

  if (rt.gpEpochOldest) {
    long days = (long)(now - rt.gpEpochOldest) / 86400;
    snprintf(v, sizeof(v), T(S_DAYS), days);
    row(T(S_ST_EPOCH), v);
  }

  ui::fmtAge(a, sizeof(a), rt.cloudFetchedAt, now);
  row(T(S_ST_CLOUD), a);

  ui::fmtAge(a, sizeof(a), rt.lastNtpAt, now);
  {
    uint32_t err = timeErrorEstimateSec();
    if (err == 0xFFFFFFFFUL)
      snprintf(v, sizeof(v), T(S_DRIFT_UNK), a);
    else
      snprintf(v, sizeof(v), T(S_DRIFT), a, (unsigned long)err);
  }
  row(T(S_ST_TIMESYNC), v);

  ui::fmtAge(a, sizeof(a), rt.lastNetOkAt, now);
  snprintf(v, sizeof(v), T(S_FAILS), a, rt.netFailStreak);
  row(T(S_ST_LASTNET), v);

  float bv = batteryVolts();
  if (bv == bv) snprintf(v, sizeof(v), T(S_BATT_FMT),
                         ui::battPercent(bv), bv,
                         usbPowered() ? T(S_CHARGING) : "");
  else          snprintf(v, sizeof(v), T(S_NOT_MEASURED));
  row(T(S_ST_BATT), v);

  snprintf(v, sizeof(v), T(S_TZ_OFF), timezoneName(),
           applyTimezone(cfg.tz) / 3600);
  row(T(S_ST_TZ), v);

  // Free space is only queried here. On a FAT32 card that walks the whole
  // allocation table, which has no place in a routine wake-up.
  {
    int32_t kb = storeFreeKb();
    if (kb < 0)
      snprintf(v, sizeof(v), "%s", storeName());
    else if (kb > 4096)
      snprintf(v, sizeof(v), T(S_FREE_MB), storeName(), (long)(kb / 1024));
    else
      snprintf(v, sizeof(v), T(S_FREE_KB), storeName(), (long)kb);
  }
  row(T(S_ST_STORE), v);

  snprintf(v, sizeof(v), "%.4f, %.4f", (double)cfg.lat, (double)cfg.lon);
  row(T(S_ST_SITE), v);

  snprintf(v, sizeof(v), "%s · %s", sysBoardName(), FW_VERSION);
  row(T(S_ST_FW), v);

  setFont(FONT_S);
  ui::text(f.content.x, (int16_t)(f.content.bottom() - textHeight()),
           T(S_HOLD_BTN),
           ALIGN_LEFT, INK_MID);

  ui::drawStatus(f, now);
}

void portal(const char* ssid, const char* pw, const char* ip) {
  dispSetMode(MODE_MONO);
  dispClear();

  layout::Frame f;
  layout::buildFrame(f);
  ui::drawHeader(f, T(S_SETUP), 0);

  const Metrics& m = metrics();

  // A QR code with the details needed to join the network. Both generating
  // and drawing are done by the HAL, because the generator is a board matter
  // rather than a layout one.
  char payload[128];
  snprintf(payload, sizeof(payload), "WIFI:T:WPA;S:%s;P:%s;;", ssid, pw);

  int16_t side = (int16_t)(f.content.h * 4 / 5);
  int16_t qx = f.content.x;
  int16_t qy = (int16_t)(f.content.y + (f.content.h - side) / 2);
  bool haveQr = drawQr(qx, qy, side, payload);
  if (!haveQr) side = 0;

  int16_t tx = (int16_t)(qx + side + (side ? m.gap * 3 : 0));
  int16_t y  = (int16_t)(f.content.y + m.gap);

  setFont(FONT_L);
  ui::text(tx, (int16_t)(y + textAscent()),
           T(S_CONNECT_TO), ALIGN_LEFT, INK_BLACK);
  y = (int16_t)(y + textHeight() + m.gap);

  setFont(FONT_XL);
  ui::text(tx, (int16_t)(y + textAscent()), ssid, ALIGN_LEFT, INK_BLACK);
  y = (int16_t)(y + textHeight() + m.gap / 2);
  ui::text(tx, (int16_t)(y + textAscent()), pw, ALIGN_LEFT, INK_BLACK);
  y = (int16_t)(y + textHeight() + m.gap * 2);

  setFont(FONT_M);
  char line[64];
  snprintf(line, sizeof(line), T(S_THEN_OPEN), ip);
  ui::text(tx, (int16_t)(y + textAscent()), line, ALIGN_LEFT, INK_DARK);
  y = (int16_t)(y + textHeight() + m.gap);
  ui::text(tx, (int16_t)(y + textAscent()),
           T(S_AP_TIMEOUT), ALIGN_LEFT, INK_MID);
}

}  // namespace screens
