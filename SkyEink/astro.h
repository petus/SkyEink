// astro.h - Sun, Moon and planets, computed locally with no network.
//
// The accuracy targets a display, not an ephemeris. Sun and Moon to a few
// arcminutes, planets to a tenth of a degree. That is ample for azimuth and
// elevation above the horizon.

#pragma once
#include <stdint.h>
#include <time.h>

namespace astro {

struct Horiz {
  double az;    // degrees from north through east
  double el;    // degrees above the horizon, no refraction
};

enum Planet : uint8_t {
  PL_MERCURY = 0, PL_VENUS, PL_MARS, PL_JUPITER, PL_SATURN, PL_COUNT
};

const char* planetName(Planet p);

double julianDay(time_t utc);

// Greenwich sidereal time in degrees. Needed by the geometric pre-filter in
// passes.cpp, which puts the site into an inertial frame.
double gmstDeg(time_t utc);

// Broken down UTC to Unix time. Written by hand because timegm() is not
// available on every toolchain.
time_t tmToUtc(const struct tm& t);

// Sun and Moon in the observer sky.
Horiz sunAt(time_t utc, double latDeg, double lonDeg);
Horiz moonAt(time_t utc, double latDeg, double lonDeg);

// Solar elevation at a given point. Also used to test whether a satellite is
// sunlit, by passing its sub-satellite point.
double sunElevation(time_t utc, double latDeg, double lonDeg);

// Unit vector towards the Sun in the observer topocentric frame.
void sunUnitVector(time_t utc, double latDeg, double lonDeg,
                   double& e, double& n, double& u);

// Illuminated fraction of the disc, 0 to 1. waxing = true means waxing.
double moonPhase(time_t utc, bool& waxing);

Horiz planetAt(Planet p, time_t utc, double latDeg, double lonDeg);
double planetMag(Planet p, time_t utc);

// The first time after "from" when solar elevation crosses elevDeg in the
// given direction. descending = true looks for a setting crossing. Returns 0
// when it does not happen within the next 36 hours, i.e. polar day or night.
time_t sunCrossing(time_t from, double elevDeg, bool descending,
                   double latDeg, double lonDeg);

// The darkness window around a given time. Returns false when the Sun never
// gets below elevDeg, which happens over northern Europe in summer at -18.
bool darkWindow(time_t around, double elevDeg,
                double latDeg, double lonDeg,
                time_t& start, time_t& end);

}  // namespace astro
