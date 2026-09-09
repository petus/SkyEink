// layout.cpp - every coordinate in the project, in one place.

#include "layout.h"

namespace layout {

namespace {

int16_t imax(int16_t a, int16_t b) { return a > b ? a : b; }
int16_t imin(int16_t a, int16_t b) { return a < b ? a : b; }

int16_t clamp16(int16_t v, int16_t lo, int16_t hi) {
  return v < lo ? lo : (v > hi ? hi : v);
}

// A rounded share of a total, so the bands add up to the content height.
int16_t part(int16_t total, int16_t num, int16_t den) {
  return (int16_t)(((int32_t)total * num + den / 2) / den);
}

bool overlaps(const Rect& a, const Rect& b) {
  return a.x < b.right() && b.x < a.right() &&
         a.y < b.bottom() && b.y < a.bottom();
}

bool inside(const Rect& a, const Rect& box) {
  return a.x >= box.x && a.y >= box.y &&
         a.right() <= box.right() && a.bottom() <= box.bottom();
}

}  // namespace

void buildFrame(Frame& f) {
  const hal::DispCaps& d = hal::dispCaps();
  const hal::Metrics&  m = hal::metrics();

  f.screen = {0, 0, d.w, d.h};

  // Two rows: title and the large clock on top, date and refresh times
  // below. Three row heights is the minimum that fits an XL and an S font.
  const int16_t headerH = imax(48, (int16_t)(m.rowH * 3));
  const int16_t statusH = imax(16, (int16_t)(m.rowH * 9 / 10));

  f.header = {m.pad, m.pad, (int16_t)(d.w - 2 * m.pad), headerH};

  f.ruleHeaderY = f.header.bottom() + m.gap / 2;
  f.status = {m.pad, (int16_t)(d.h - m.pad - statusH),
              (int16_t)(d.w - 2 * m.pad), statusH};
  f.ruleStatusY = f.status.y - m.gap / 2;

  const int16_t contentY = f.ruleHeaderY + m.ruleW + m.gap;
  f.content = {m.pad, contentY, (int16_t)(d.w - 2 * m.pad),
               (int16_t)(f.ruleStatusY - m.gap - contentY)};
}

void buildNight(uint8_t cloudHours, Night& n) {
  const hal::Metrics& m = hal::metrics();

  buildFrame(n.frame);
  const Rect& c = n.frame.content;

  // The table row height is the basic unit of the whole screen.
  n.rowH = imax(16, m.rowH);
  const int16_t labelH = imax(14, (int16_t)(n.rowH * 4 / 5));

  // Three bands. The table gets the largest share because it is the only
  // part where the row count directly changes how useful the screen is.
  const int16_t gaps = 2 * m.gap;
  const int16_t usable = (int16_t)(c.h - gaps);

  // The weather band gets more because the columns now carry temperature
  // and precipitation under the hour, not just the hour.
  int16_t cloudH   = part(usable, 37, 100);
  int16_t tableH   = part(usable, 37, 100);
  // The horizon band takes what is left, see below.
  int16_t horizonH = (int16_t)(usable - cloudH - tableH);

  // A table under three rows is pointless. If it does not fit, the horizon
  // band gives way.
  const int16_t tableMin = (int16_t)(labelH + n.rowH * 7 / 10 + 3 * n.rowH);
  if (tableH < tableMin) {
    const int16_t need = (int16_t)(tableMin - tableH);
    const int16_t take = imin(need, (int16_t)(horizonH - 3 * labelH));
    if (take > 0) { horizonH = (int16_t)(horizonH - take); tableH = (int16_t)(tableH + take); }
  }

  // ---- weather band
  // Three rows under the bars: hour, temperature, precipitation. The panel
  // is over 280 dpi, so a twelve point font is physically only a millimetre
  // and a half tall. Temperature is the headline value and gets the most.
  n.rowHour   = n.rowH;
  n.rowTemp   = (int16_t)(n.rowH * 7 / 5);
  n.rowPrecip = n.rowH;

  // The columns have to stay legible. When the labels and the bars do not
  // fit the band, the layout gives way in steps: precipitation goes first,
  // then the temperature row shrinks. Showing fewer values beats showing all
  // of them squashed. The drawing code follows these numbers, so a height of
  // zero means the row is not drawn at all.
  const int16_t barsMin = (int16_t)(n.rowH * 3 / 2);
  int16_t hoursH = (int16_t)(n.rowHour + n.rowTemp + n.rowPrecip);
  if (cloudH - labelH - hoursH < barsMin) {
    n.rowPrecip = 0;
    hoursH = (int16_t)(n.rowHour + n.rowTemp);
  }
  if (cloudH - labelH - hoursH < barsMin) {
    n.rowTemp = n.rowH;
    hoursH = (int16_t)(n.rowHour + n.rowTemp);
  }
  n.cloud      = {c.x, c.y, c.w, cloudH};
  n.cloudLabel = {c.x, c.y, c.w, labelH};
  n.cloudHours = {c.x, (int16_t)(n.cloud.bottom() - hoursH), c.w, hoursH};
  n.cloudBars  = {c.x, (int16_t)(n.cloudLabel.bottom() + m.gap / 2), c.w,
                  (int16_t)(n.cloudHours.y - m.gap / 2 - n.cloudLabel.bottom() - m.gap / 2)};

  // A column has to be wider than its label, or the hours run together.
  const int16_t hourLabelW = imax(18, (int16_t)(m.scale * 26));
  uint8_t cols = cloudHours ? cloudHours : 1;
  n.cloudBarGap = imax(2, (int16_t)(m.gap / 2));
  while (cols > 4) {
    int16_t w = (int16_t)((n.cloudBars.w - (cols - 1) * n.cloudBarGap) / cols);
    if (w >= hourLabelW) break;
    cols--;
  }
  n.cloudCols = cols;
  n.cloudBarW = (int16_t)((n.cloudBars.w - (cols - 1) * n.cloudBarGap) / cols);

  // ---- pass table band
  const int16_t tableY = (int16_t)(n.cloud.bottom() + m.gap);
  n.table      = {c.x, tableY, c.w, tableH};
  n.tableLabel = {c.x, tableY, c.w, labelH};
  n.tableHead  = {c.x, (int16_t)(n.tableLabel.bottom()), c.w,
                  (int16_t)(n.rowH * 7 / 10)};
  n.tableBody  = {c.x, n.tableHead.bottom(), c.w,
                  (int16_t)(n.table.bottom() - n.tableHead.bottom())};
  n.rows = (uint8_t)clamp16((int16_t)(n.tableBody.h / n.rowH), 0, 8);

  // Column shares, adding up to 100. The rounding remainder goes to the name
  // column, the only one whose content varies in length.
  static const uint8_t frac[COL_COUNT] = {11, 29, 21, 11, 11, 17};
  int16_t x = c.x;
  for (uint8_t i = 0; i < COL_COUNT; i++) {
    int16_t w = (i == COL_COUNT - 1) ? (int16_t)(c.right() - x)
                                     : part(c.w, frac[i], 100);
    n.colX[i] = x;
    n.colW[i] = w;
    x = (int16_t)(x + w);
  }

  // ---- horizon band and the Moon
  const int16_t horY = (int16_t)(n.table.bottom() + m.gap);
  n.horizon      = {c.x, horY, c.w, (int16_t)(c.bottom() - horY)};
  n.horizonLabel = {c.x, horY, c.w, labelH};

  int16_t moonW = part(c.w, 26, 100);
  const int16_t plotH = (int16_t)(n.horizon.bottom() - n.horizonLabel.bottom() - labelH - m.gap / 2);

  // One line has to be left under the disc for the phase caption. Without
  // that reserve the text spilled into the status bar and covered the
  // firmware version.
  const int16_t moonTextH = (int16_t)(n.rowH + m.gap / 2);
  n.moonR = imin((int16_t)((plotH - moonTextH) / 2), (int16_t)(moonW / 3));
  n.moonR = imax(n.moonR, (int16_t)(8 * m.scale));

  n.moon = {(int16_t)(c.right() - moonW), (int16_t)(n.horizonLabel.bottom() + m.gap / 2),
            moonW, (int16_t)(n.horizon.bottom() - n.horizonLabel.bottom() - m.gap / 2)};

  n.horizonPlot = {c.x, n.moon.y,
                   (int16_t)(n.moon.x - m.gap - c.x), plotH};
  n.horizonAxis = {n.horizonPlot.x, n.horizonPlot.bottom(),
                   n.horizonPlot.w, labelH};

  // The Moon caption sits under the disc, right aligned. It must never pass
  // the bottom edge of the band or it reaches the status bar.
  const int16_t moonTextY = (int16_t)(n.moon.y + 2 * n.moonR + m.gap / 2);
  n.moonText = {n.moon.x, moonTextY, n.moon.w,
                (int16_t)(n.moon.bottom() - moonTextY)};
  if (n.moonText.h < 0) n.moonText.h = 0;
  if (n.moonText.bottom() > n.horizon.bottom())
    n.moonText.h = (int16_t)(n.horizon.bottom() - n.moonText.y);
}

void buildPass(Pass& p) {
  const hal::Metrics& m = hal::metrics();
  buildFrame(p.frame);
  const Rect& c = p.frame.content;

  // Circle on the left, numbers on the right. The plot square must exceed
  // neither the content height nor half the width, so the right column keeps
  // usable space.
  const int16_t dialSide = imin((int16_t)(c.h), part(c.w, 46, 100));
  p.dial  = {c.x, (int16_t)(c.y + (c.h - dialSide) / 2), dialSide, dialSide};

  // The compass labels live inside the square, in the strip between the
  // circle and the edge. The circle shrinks by that strip.
  p.dialLabelH = imax(14, m.rowH);
  p.dialR = (int16_t)(dialSide / 2 - p.dialLabelH);
  if (p.dialR < 10) p.dialR = 10;

  const int16_t rightX = (int16_t)(p.dial.right() + m.gap * 2);
  const int16_t rightW = (int16_t)(c.right() - rightX);

  // The countdown is the headline value and gets twice the row height.
  const int16_t cdH = imax(32, (int16_t)(m.rowH * 2));
  p.countdown = {rightX, c.y, rightW, cdH};

  // A fact row carries a label and a value side by side, value in a large
  // font.
  p.factRowH = imax(20, (int16_t)(m.rowH * 3 / 2));
  p.facts = {rightX, (int16_t)(p.countdown.bottom() + m.gap), rightW,
             (int16_t)(c.bottom() - p.countdown.bottom() - m.gap)};
  p.factRows = (uint8_t)clamp16((int16_t)(p.facts.h / p.factRowH), 0, 8);
}

bool validatePass(const Pass& p) {
  const Rect& s = p.frame.screen;
  const Rect* all[] = {&p.frame.header, &p.frame.content, &p.frame.status,
                       &p.dial, &p.countdown, &p.facts};
  for (const Rect* r : all) {
    if (r->w < 0 || r->h < 0) return false;
    if (!inside(*r, s)) return false;
  }
  // The plot must not meet the right column, the header or the status bar.
  if (overlaps(p.dial, p.countdown)) return false;
  if (overlaps(p.dial, p.facts)) return false;
  if (overlaps(p.countdown, p.facts)) return false;
  if (overlaps(p.dial, p.frame.header)) return false;
  if (overlaps(p.dial, p.frame.status)) return false;
  // Circle plus labels have to stay inside their square.
  if (p.dialR + p.dialLabelH > p.dial.w / 2) return false;
  if (p.dialR < 10) return false;
  if (p.factRows < 4) return false;
  return true;
}

void buildProgress(Progress& p) {
  const hal::Metrics& m = hal::metrics();
  buildFrame(p.frame);
  const Rect& c = p.frame.content;

  const int16_t lineH = imax(18, m.rowH);
  const int16_t barH  = imax(14, (int16_t)(m.rowH * 4 / 5));
  const int16_t blockH = (int16_t)(lineH + m.gap + barH + m.gap + lineH);
  int16_t y = (int16_t)(c.y + (c.h - blockH) / 2);
  if (y < c.y) y = c.y;

  const int16_t barW = part(c.w, 70, 100);
  p.caption  = {c.x, y, c.w, lineH};
  p.bar      = {(int16_t)(c.x + (c.w - barW) / 2), (int16_t)(y + lineH + m.gap),
                barW, barH};
  p.estimate = {c.x, (int16_t)(p.bar.bottom() + m.gap), c.w, lineH};
}

bool validateNight(const Night& n) {
  const Rect& s = n.frame.screen;
  const Rect* all[] = {&n.frame.header, &n.frame.content, &n.frame.status,
                       &n.cloud, &n.cloudLabel, &n.cloudBars, &n.cloudHours,
                       &n.table, &n.tableLabel, &n.tableHead, &n.tableBody,
                       &n.horizon, &n.horizonLabel, &n.horizonPlot,
                       &n.horizonAxis, &n.moon, &n.moonText};
  for (const Rect* r : all) {
    if (r->w < 0 || r->h < 0) return false;
    if (!inside(*r, s)) return false;
  }
  // The three bands must not intersect each other, the header or the bar.
  const Rect* bands[] = {&n.frame.header, &n.cloud, &n.table, &n.horizon,
                         &n.frame.status};
  for (uint8_t i = 0; i < 5; i++)
    for (uint8_t j = (uint8_t)(i + 1); j < 5; j++)
      if (overlaps(*bands[i], *bands[j])) return false;

  if (overlaps(n.horizonPlot, n.moon)) return false;
  if (n.rows < 3) return false;
  if (n.cloudCols < 4) return false;
  if (n.cloudBarW < 1) return false;
  if (n.cloudBars.h < n.rowH) return false;

  // The last table column must not overflow.
  if (n.colX[COL_COUNT - 1] + n.colW[COL_COUNT - 1] > n.frame.content.right())
    return false;
  return true;
}

}  // namespace layout
