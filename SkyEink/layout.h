// layout.h - screen layout computed from the panel size.
//
// RULE: not one pixel coordinate may appear in the drawing code. Everything
// comes from here, so porting to another panel is a different input to the
// build functions rather than a rewrite of the drawing.

#pragma once
#include <stdint.h>
#include "hal.h"

namespace layout {

struct Rect {
  int16_t x, y, w, h;
  int16_t right()  const { return x + w; }
  int16_t bottom() const { return y + h; }
  int16_t cx()     const { return x + w / 2; }
  int16_t cy()     const { return y + h / 2; }
};

// Columns of the pass table.
enum Col : uint8_t {
  COL_TIME = 0, COL_NAME, COL_DIR, COL_MAXEL, COL_MAG, COL_DUR, COL_COUNT
};

// The frame shared by every screen. Header on top, status bar at the bottom,
// content in between. The bar is permanent, not just shown on a fault.
struct Frame {
  Rect screen;
  Rect header;
  Rect content;
  Rect status;
  int16_t ruleHeaderY;   // rule below the header
  int16_t ruleStatusY;   // rule above the status bar
};

// The main night screen.
struct Night {
  Frame   frame;

  Rect    cloud;         // the whole weather band
  Rect    cloudLabel;    // heading row of the band
  Rect    cloudBars;     // area of the bars
  Rect    cloudHours;    // label rows under the bars
  uint8_t cloudCols;     // number of hourly columns
  int16_t cloudBarW;     // width of a single column
  int16_t cloudBarGap;
  // Three label rows under the bars: hour, temperature, precipitation. They
  // differ in height because temperature is the headline value and carries
  // the largest font.
  int16_t rowHour, rowTemp, rowPrecip;

  Rect    table;         // the whole pass table band
  Rect    tableLabel;
  Rect    tableHead;     // column header row
  Rect    tableBody;
  int16_t rowH;
  uint8_t rows;          // how many rows fit
  int16_t colX[COL_COUNT];   // left edge of each column
  int16_t colW[COL_COUNT];

  Rect    horizon;       // horizon band with the planets
  Rect    horizonLabel;
  Rect    horizonPlot;   // plot area, the bottom edge is the horizon
  Rect    horizonAxis;   // azimuth label row
  Rect    moon;          // Moon panel on the right
  int16_t moonR;         // disc radius
  Rect    moonText;      // caption under the disc
};

// The pass detail screen.
struct Pass {
  Frame   frame;
  // The whole plot square including the compass labels. The circle is
  // smaller because the labels have to stay inside this area. They used to be
  // drawn outside it and ran into the header and the status bar.
  Rect    dial;
  int16_t dialR;         // circle radius
  int16_t dialLabelH;    // label strip between circle and square edge
  Rect    countdown;     // the large countdown
  Rect    facts;         // magnitude, elevation, direction
  int16_t factRowH;
  uint8_t factRows;
};

// The recomputation progress screen. Drawn in MODE_MONO so that partial
// refresh is available, which means it has to stay deliberately plain.
struct Progress {
  Frame  frame;
  Rect   caption;        // what is happening
  Rect   bar;            // progress bar outline
  Rect   estimate;       // percentage line
};

// Builds the layout for the current panel. Call after hal::dispBegin().
// cloudHours is how many hours should fit in the weather band; when that many
// would not stay legible, the function reduces the count itself and writes
// the real value to out.cloudCols.
void buildNight(uint8_t cloudHours, Night& out);
void buildPass(Pass& out);
void buildProgress(Progress& out);

// The shared frame on its own, for screens that lay out their own content.
void buildFrame(Frame& out);

// Development check: false when a rectangle falls outside the panel or two
// elements that must not overlap do. Not called in a normal run.
bool validateNight(const Night& n);
bool validatePass(const Pass& p);

}  // namespace layout
