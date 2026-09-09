// passes.cpp
//
// Two things keep the computation within a sensible budget.
//
// 1. A geometric pre-filter. Before SGP4 is started, the orbital plane tells
//    us whether the object can reach the site during this night at all. That
//    is a handful of multiplications, whereas one SGP4 propagation costs
//    about three milliseconds on an ESP32. Most of the catalogue is rejected
//    essentially for free.
//
// 2. The pass finder from the library. Sgp4::nextpass() steps orbit by orbit
//    and locates the elevation maximum with Brent's method. An earlier
//    version of this file sampled the whole night every thirty seconds
//    instead, which was roughly five times as expensive.

#include <Arduino.h>
#include <math.h>
#include <string.h>
#include <Sgp4.h>
#include "passes.h"
#include "gp.h"
#include "astro.h"
#include "config.h"
#include "i18n.h"
#include "runtime.h"
#include "log.h"

namespace passes {

namespace {

const double DEG = M_PI / 180.0;
const double RE  = 6378.137;         // equatorial radius of the Earth, km
const double MU  = 398600.4418;      // gravitational parameter, km^3/s^2
const double J2  = 1.08262668e-3;

// Pre-filter step. The orbital plane moves slowly relative to the site, so
// ten minutes is ample. The margin below covers the coarseness.
const int  FILTER_STEP   = 600;
const double FILTER_SLACK = 6.0;     // extra degrees so the filter is not tight

Sgp4 sat;
gp::Rec cur;
bool    haveTrack = false;

double jdToUnix(double jd) { return (jd - 2440587.5) * 86400.0; }

// Semi-major axis from mean motion.
double semiMajor(double revPerDay) {
  double n = revPerDay * 2.0 * M_PI / 86400.0;   // rad/s
  if (n <= 0) return 0;
  return pow(MU / (n * n), 1.0 / 3.0);
}

// Nodal precession in degrees per second. Without it, the plane of a low
// orbit would drift further over one night than the filter margin allows.
double nodeRate(double a, double e, double incDeg, double revPerDay) {
  if (a <= 0) return 0;
  double n = revPerDay * 2.0 * M_PI / 86400.0;
  double p = a * (1.0 - e * e);
  if (p <= 0) return 0;
  double rate = -1.5 * J2 * (RE / p) * (RE / p) * n * cos(incDeg * DEG);
  return rate / DEG;   // rad/s to deg/s
}

// The largest angular distance from the site to the sub-satellite point at
// which the object is still above the minimum elevation.
double maxGroundAngle(double altKm, double minElDeg) {
  double el = minElDeg * DEG;
  double c = RE * cos(el) / (RE + altKm);
  if (c > 1.0) c = 1.0;
  return (acos(c) - el) / DEG;
}

// Can the object reach the site during this window at all? The angle between
// the site vector and the orbital plane is evaluated; while it stays larger
// than the reach, no pass is possible and SGP4 is never started.
bool planePossible(const gp::Rec& r, time_t from, time_t to) {
  double a = semiMajor(r.mm);
  if (a <= RE) return false;                    // nonsense elements
  double reach = maxGroundAngle(a - RE, (double)cfg.minElevDeg) + FILTER_SLACK;
  if (reach >= 90.0) return true;

  double rate = nodeRate(a, r.ecc, r.inc, r.mm);
  double si = sin(r.inc * DEG), ci = cos(r.inc * DEG);
  double lat = (double)cfg.lat * DEG;
  double clat = cos(lat), slat = sin(lat);

  for (time_t t = from; t <= to; t += FILTER_STEP) {
    // Right ascension of the node at time t, precessed from the epoch.
    double om = (r.raan + rate * (double)(t - (time_t)r.epoch)) * DEG;

    // Normal of the orbital plane in the inertial frame.
    double nx = si * sin(om), ny = -si * cos(om), nz = ci;

    // Unit vector of the site in the same frame.
    double th = (astro::gmstDeg(t) + (double)cfg.lon) * DEG;
    double rx = clat * cos(th), ry = clat * sin(th), rz = slat;

    // Angle of the site above the orbital plane.
    double s = nx * rx + ny * ry + nz * rz;
    if (s > 1.0) s = 1.0;
    if (s < -1.0) s = -1.0;
    if (fabs(asin(s) / DEG) <= reach) return true;
  }
  return false;
}

// Visual magnitude estimate. Standard formula with a diffuse phase function.
// The solar position is not computed here. Sgp4::findsat() calls visible(),
// which fills sunAz and sunEl for the site at exactly this instant. This code
// used to recompute it via astro::sunUnitVector() for nothing.
double magnitude(double stdMag, double rangeKm, double satAz, double satEl) {
  const double sa = sat.sunAz * DEG, sl = sat.sunEl * DEG;
  const double se = cos(sl) * sin(sa);
  const double sn = cos(sl) * cos(sa);
  const double su = sin(sl);

  double a = satAz * DEG, e = satEl * DEG;
  double xe = cos(e) * sin(a), xn = cos(e) * cos(a), xu = sin(e);

  // The distance to the Sun dwarfs the distance to the satellite, so the
  // direction to the Sun is practically the same from both points.
  double cosPhase = -(xe * se + xn * sn + xu * su);
  if (cosPhase >  1.0) cosPhase =  1.0;
  if (cosPhase < -1.0) cosPhase = -1.0;
  double ph = acos(cosPhase);

  double term = sin(ph) + (M_PI - ph) * cos(ph);
  if (term < 1e-6) term = 1e-6;

  return stdMag - 15.0 + 5.0 * log10(rangeKm) - 2.5 * log10(term);
}

// Pozor na jmeno parametru: sgp4unit.h ma #define pi, takze
// promenna jmenem pi se rozvine na konstantu.
// Two records describe the same event when they peak at nearly the same time
// in nearly the same direction. That is either a composite body listed more
// than once in the catalogue, or a fresh Starlink train flying nose to tail.
void mergeDuplicates() {
  const long DT = 180;      // sekund mezi vrcholy
  const int  DAZ = 20;      // degrees of azimuth at the peak

  for (uint8_t i = 0; i < rt.passCount; i++) {
    for (uint8_t j = (uint8_t)(i + 1); j < rt.passCount; ) {
      long dt = (long)(rt.passes[j].tMax - rt.passes[i].tMax);
      if (dt < 0) dt = -dt;
      int daz = rt.passes[j].azMax - rt.passes[i].azMax;
      if (daz < 0) daz = -daz;
      if (daz > 180) daz = 360 - daz;

      if (dt > DT || daz > DAZ) { j++; continue; }

      bool sameName = strncmp(rt.passes[i].name, rt.passes[j].name,
                              SKY_NAME_LEN) == 0;
      // Keep the brighter of the two.
      if (rt.passes[j].mag < rt.passes[i].mag) {
        uint8_t g = (uint8_t)(rt.passes[i].group + rt.passes[j].group);
        bool m = rt.passes[i].mixed || rt.passes[j].mixed || !sameName;
        rt.passes[i] = rt.passes[j];
        rt.passes[i].group = g;
        rt.passes[i].mixed = m;
      } else {
        rt.passes[i].group = (uint8_t)(rt.passes[i].group + rt.passes[j].group);
        if (!sameName) rt.passes[i].mixed = true;
      }

      rt.passCount--;
      for (uint8_t k = j; k < rt.passCount; k++) rt.passes[k] = rt.passes[k + 1];
    }
  }
}

void addPass(const passinfo& pinf, const gp::Rec& r, double mag) {
  if (rt.passCount >= SKY_MAX_PASSES) {
    // Replace the faintest one if this pass is brighter.
    uint8_t worst = 0;
    for (uint8_t i = 1; i < rt.passCount; i++)
      if (rt.passes[i].mag > rt.passes[worst].mag) worst = i;
    if (rt.passes[worst].mag <= mag) return;
    rt.passCount = (uint8_t)(rt.passCount - 1);
    for (uint8_t i = worst; i < rt.passCount; i++)
      rt.passes[i] = rt.passes[i + 1];
  }

  PassRec& p = rt.passes[rt.passCount++];
  memset(&p, 0, sizeof(p));
  p.group  = 1;
  p.tRise  = (time_t)jdToUnix(pinf.jdstart);
  p.tMax   = (time_t)jdToUnix(pinf.jdmax);
  p.tSet   = (time_t)jdToUnix(pinf.jdstop);
  p.azRise = (int16_t)lround(pinf.azstart);
  p.azMax  = (int16_t)lround(pinf.azmax);
  p.azSet  = (int16_t)lround(pinf.azstop);
  p.elMax  = (int16_t)lround(pinf.maxelevation);
  p.mag    = (float)mag;
  p.norad  = r.norad;
  strncpy(p.name, r.name, SKY_NAME_LEN - 1);
}

}  // namespace

int compute(time_t around, ProgressFn cb) {
  if (!gp::open()) return -1;
  uint16_t n = gp::count();
  if (n > cfg.maxObjects) n = cfg.maxObjects;

  // Darkness window. Astronomical night first; when summer never gets that
  // dark, fall back to nautical and then to civil twilight.
  time_t darkStart = 0, darkEnd = 0;
  if (!astro::darkWindow(around, -18.0, cfg.lat, cfg.lon, darkStart, darkEnd) &&
      !astro::darkWindow(around, -12.0, cfg.lat, cfg.lon, darkStart, darkEnd) &&
      !astro::darkWindow(around,  -6.0, cfg.lat, cfg.lon, darkStart, darkEnd)) {
    gp::close();
    return 0;
  }

  rt.passCount = 0;
  sat.site((double)cfg.lat, (double)cfg.lon, (double)cfg.altM);
  sat.setsunrise(-6.0);          // observer counts as dark below this

  const double nightHours = (double)(darkEnd - darkStart) / 3600.0;
  uint32_t t0 = millis();
  uint16_t tried = 0, skipped = 0;

  for (uint16_t i = 0; i < n; i++) {
    if (cb && (i % 16) == 0)
      cb(T(S_P_PASSES), (uint8_t)(10 + (uint32_t)i * 85 / n));

    // The computation runs for tens of seconds without a break. Without a
    // short yield the system idle task never runs and the watchdog would
    // restartoval.
    if ((i % 8) == 0) delay(1);

    if (!gp::get(i, cur)) continue;
    if (cur.mm <= 0.0f) continue;

    if (!planePossible(cur, darkStart, darkEnd)) { skipped++; continue; }
    tried++;

    if (!sat.init(cur.name, cur.l1, cur.l2)) continue;

    // How many orbits fit into the night, plus a margin.
    int orbits = (int)(nightHours * (double)cur.mm / 24.0) + 2;
    if (orbits > 20) orbits = 20;

    if (!sat.initpredpoint((unsigned long)darkStart, 0.0)) continue;

    passinfo pinf;
    for (int k = 0; k < 6; k++) {
      if (!sat.nextpass(&pinf, orbits, false, (double)cfg.minElevDeg)) break;

      time_t rise = (time_t)jdToUnix(pinf.jdstart);
      if (rise > darkEnd) break;
      if (pinf.jdmax <= 0.0) break;

      // The library decides whether the satellite is lit and the observer is
      // in the dark. Anything other than lighted is not worth listing.
      if (pinf.sight != lighted) continue;

      time_t tmax = (time_t)jdToUnix(pinf.jdmax);
      if (tmax < darkStart || tmax > darkEnd) continue;

      sat.findsat((unsigned long)tmax);
      double mag = magnitude(cur.stdMag, sat.satDist, sat.satAz, sat.satEl);
      if (mag > (double)cfg.magLimit) continue;

      addPass(pinf, cur, mag);
    }
  }

  uint32_t elapsed = millis() - t0;
  if (n) rt.scanMsPerObject100 = (elapsed * 100) / n;

  rtSortPasses();
  mergeDuplicates();
  rt.passesComputedAt = around;
  rt.passesValidTo    = darkEnd;
  rt.gpCount          = gp::count();
  gp::close();

  LOGF("prelety: okno %.1f h, propagovano %u, predfiltrem vyrazeno %u, "
       "nalezeno %u, %lu ms", nightHours, tried, skipped, rt.passCount,
       (unsigned long)elapsed);

  if (cb) cb(T(S_P_DONE), 100);
  return rt.passCount;
}

bool trackBegin(uint16_t passIndex) {
  haveTrack = false;
  if (passIndex >= rt.passCount) return false;
  if (!gp::open()) return false;

  gp::Rec r;
  bool found = false;
  for (uint16_t i = 0; i < gp::count(); i++) {
    if (gp::get(i, r) && r.norad == rt.passes[passIndex].norad) { found = true; break; }
  }
  gp::close();
  if (!found) return false;

  sat.site((double)cfg.lat, (double)cfg.lon, (double)cfg.altM);
  if (!sat.init(r.name, r.l1, r.l2)) return false;
  haveTrack = true;
  return true;
}

bool trackAt(time_t t, float& azOut, float& elOut) {
  if (!haveTrack) return false;
  sat.findsat((unsigned long)t);
  azOut = (float)sat.satAz;
  elOut = (float)sat.satEl;
  return sat.satEl > 0.0;
}

void trackEnd() { haveTrack = false; }

}  // namespace passes
