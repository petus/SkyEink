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
#include "hal.h"
#include "astro.h"
#include "config.h"
#include "i18n.h"
#include "version.h"
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

// Beyond this the SGP4 prediction drifts far enough that the pass time would
// be wrong by minutes. Such objects are skipped one by one instead of
// invalidating the whole catalogue.
const long MAX_EPOCH_AGE_S = 14L * 86400L;

// Solar elevation below which the observer counts as being in the dark.
// The same number goes into Sgp4::setsunrise() and into the search window,
// otherwise the two disagree and passes get thrown away before the library
// ever sees them. Civil twilight is what Heavens-Above uses as well, and it
// is when satellite passes are at their best: the satellite is still sunlit
// while the sky is already dark.
const double SUN_DARK_DEG = -6.0;

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

// One propagated instant. findsat() fills the satellite position, the
// topocentric azimuth and elevation, and the solar azimuth and elevation at
// the site, so everything the refinement below needs comes from one call.
struct Sample {
  double az, el, range, lat, lon, alt;
};

void sample(time_t t, Sample& s) {
  sat.findsat((unsigned long)t);
  s.az    = sat.satAz;
  s.el    = sat.satEl;
  s.range = sat.satDist;
  s.lat   = sat.satLat;
  s.lon   = sat.satLon;
  s.alt   = sat.satAlt;
}

// Is the satellite lit at this instant? Cylindrical shadow approximation:
// a satellite at altitude h sees the Sun above its own horizon when the solar
// elevation at the sub-satellite point is above -acos(Re / (Re + h)).
bool sunlitNow(time_t t, const Sample& s) {
  const double limit = -acos(RE / (RE + s.alt)) * 180.0 / M_PI;
  return astro::sunElevation(t, s.lat, s.lon) > limit;
}

// Narrows a pass down to the part that is actually visible.
//
// nextpass() reports the geometric pass, from horizon to horizon. A satellite
// often enters the Earth's shadow partway through and vanishes while still
// climbing, so the geometric peak can be both higher and closer than anything
// anyone gets to see. Reporting that peak overstated the brightness by almost
// a magnitude against Heavens-Above.
//
// This walks the pass in ten second steps and keeps only the lit samples.
// Returns false when none of them is lit, which means nothing was visible.
bool refineToVisible(PassRec& p, float stdMag) {
  const int STEP = 10;
  bool any = false;
  Sample s;

  time_t firstLit = 0, lastLit = 0, tBest = p.tMax;
  double elBest = -90.0, magBest = 99.0;
  int16_t azFirst = 0, azLast = 0, azBest = 0;

  for (time_t t = p.tRise; t <= p.tSet; t += STEP) {
    sample(t, s);
    if (s.el <= 0.0) continue;
    if (!sunlitNow(t, s)) continue;

    if (!any) { firstLit = t; azFirst = (int16_t)lround(s.az); any = true; }
    lastLit = t;
    azLast = (int16_t)lround(s.az);

    // Everything reported about the peak comes from one and the same sample.
    // Taking the brightest instant instead looked tempting, but the phase
    // angle keeps falling after the peak, so the brightest moment sits
    // elsewhere on the track and the line would then claim a magnitude that
    // belongs to a different elevation than the one printed next to it.
    // Heavens-Above quotes the highest point too, and the numbers agree.
    if (s.el > elBest) {
      elBest  = s.el;
      azBest  = (int16_t)lround(s.az);
      tBest   = t;
      magBest = magnitude(stdMag, s.range, s.az, s.el);
    }
  }

  if (!any) return false;

  p.tRise  = firstLit;
  p.tSet   = lastLit;
  p.tMax   = tBest;
  p.azRise = azFirst;
  p.azSet  = azLast;
  p.azMax  = azBest;
  p.elMax  = (int16_t)lround(elBest);
  p.mag    = (float)magBest;
  return true;
}

void addPass(const PassRec& src, const gp::Rec& r) {
  const float mag = src.mag;
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
  p = src;
  p.group    = 1;
  p.mixed    = false;
  p.notified = false;
  p.norad    = r.norad;
  memset(p.name, 0, SKY_NAME_LEN);
  strncpy(p.name, r.name, SKY_NAME_LEN - 1);
}

}  // namespace

// ---------------------------------------------------------------- cache

namespace {

const char* PASS_FILE = "/" FW_SLUG "/passes.dat";
const uint32_t PASS_MAGIC = 0x534B5031UL;   // "SKP1"

struct PassFile {
  uint32_t magic;
  uint32_t fingerprint;
  int32_t  computedAt;
  int32_t  validTo;
  uint8_t  count;
  uint8_t  pad[3];
  PassRec  list[SKY_MAX_PASSES];
};

// FNV-1a over the settings the pass list depends on. Anything else, such as
// the language or the quiet hours, can change without invalidating it.
uint32_t settingsFingerprint() {
  uint32_t h = 2166136261UL;
  auto mix = [&h](const void* p, size_t n) {
    const uint8_t* b = (const uint8_t*)p;
    for (size_t i = 0; i < n; i++) { h ^= b[i]; h *= 16777619UL; }
  };
  mix(&cfg.lat, sizeof(cfg.lat));
  mix(&cfg.lon, sizeof(cfg.lon));
  mix(&cfg.altM, sizeof(cfg.altM));
  mix(&cfg.groups, sizeof(cfg.groups));
  mix(&cfg.maxObjects, sizeof(cfg.maxObjects));
  mix(&cfg.magLimit, sizeof(cfg.magLimit));
  mix(&cfg.minElevDeg, sizeof(cfg.minElevDeg));
  mix(&cfg.slMinMeanMotion, sizeof(cfg.slMinMeanMotion));
  mix(cfg.magRules, sizeof(cfg.magRules));
  mix(&cfg.magDefault, sizeof(cfg.magDefault));
  return h;
}

}  // namespace

void savePasses() {
  PassFile f;
  memset(&f, 0, sizeof(f));
  f.magic       = PASS_MAGIC;
  f.fingerprint = settingsFingerprint();
  f.computedAt  = (int32_t)rt.passesComputedAt;
  f.validTo     = (int32_t)rt.passesValidTo;
  f.count       = rt.passCount;
  memcpy(f.list, rt.passes, sizeof(f.list));

  hal::StoreFile h = hal::storeOpenWrite(PASS_FILE);
  if (!h) { LOGS("prelety: seznam se nepodarilo ulozit"); return; }
  size_t w = hal::storeWrite(h, &f, sizeof(f));
  hal::storeClose(h);
  if (w != sizeof(f)) LOGF("prelety: ulozeno jen %u z %u B",
                           (unsigned)w, (unsigned)sizeof(f));
  else                LOGF("prelety: seznam ulozen na kartu (%u B)",
                           (unsigned)sizeof(f));
}

bool loadPasses(time_t now) {
  if (!hal::storeExists(PASS_FILE)) return false;

  PassFile f;
  hal::StoreFile h = hal::storeOpenRead(PASS_FILE);
  if (!h) return false;
  size_t n = hal::storeRead(h, &f, sizeof(f));
  hal::storeClose(h);

  if (n != sizeof(f) || f.magic != PASS_MAGIC) return false;
  if (f.count > SKY_MAX_PASSES) return false;

  if (f.fingerprint != settingsFingerprint()) {
    LOGS("prelety z karty: nastaveni se zmenilo, prepocitava se");
    return false;
  }

  // Only useful while the window it was computed for is still running.
  if ((time_t)f.validTo <= now) {
    LOGS("prelety z karty: okno uz skoncilo, prepocitava se");
    return false;
  }
  if ((time_t)f.computedAt > now) return false;

  rt.passCount        = f.count;
  rt.passesComputedAt = (time_t)f.computedAt;
  rt.passesValidTo    = (time_t)f.validTo;
  memcpy(rt.passes, f.list, sizeof(rt.passes));

  // The list came from a previous boot, so nothing has been announced yet
  // in this one. Passes that are already over get skipped anyway.
  for (uint8_t i = 0; i < rt.passCount; i++)
    rt.passes[i].notified = rt.passes[i].tSet < now;
  return true;
}

int compute(time_t around, ProgressFn cb) {
  if (!gp::open()) return -1;
  uint16_t n = gp::count();
  if (n > cfg.maxObjects) n = cfg.maxObjects;

  // Darkness window, from civil dusk to civil dawn. It used to start at
  // astronomical night, which cut off the couple of hours right after dusk.
  // That is exactly when the brightest passes happen, so the best pass of
  // the evening was routinely missing from the list.
  time_t darkStart = 0, darkEnd = 0;
  if (!astro::darkWindow(around, SUN_DARK_DEG, cfg.lat, cfg.lon,
                         darkStart, darkEnd)) {
    gp::close();
    return 0;
  }

  rt.passCount = 0;
  sat.site((double)cfg.lat, (double)cfg.lon, (double)cfg.altM);
  sat.setsunrise(SUN_DARK_DEG);

  const double nightHours = (double)(darkEnd - darkStart) / 3600.0;
  uint32_t t0 = millis();
  uint16_t tried = 0, skipped = 0, stale = 0;

  for (uint16_t i = 0; i < n; i++) {
    if (cb && (i % 16) == 0)
      cb(T(S_P_PASSES), (uint8_t)(10 + (uint32_t)i * 85 / n));

    // The computation runs for tens of seconds without a break. Without a
    // short yield the system idle task never runs and the watchdog would
    // restartoval.
    if ((i % 8) == 0) delay(1);

    if (!gp::get(i, cur)) continue;
    if (cur.mm <= 0.0f) continue;

    // Elements that are too old are dropped individually.
    long epochAge = (long)(around - (time_t)cur.epoch);
    if (epochAge > MAX_EPOCH_AGE_S) { stale++; continue; }

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

      // A generous first cut on the geometric peak. The refinement below can
      // only make the pass fainter, never brighter, so anything already too
      // faint here is not worth refining.
      if (mag > (double)cfg.magLimit) continue;

      PassRec cand;
      memset(&cand, 0, sizeof(cand));
      cand.tRise  = (time_t)jdToUnix(pinf.jdstart);
      cand.tMax   = (time_t)jdToUnix(pinf.jdmax);
      cand.tSet   = (time_t)jdToUnix(pinf.jdstop);
      cand.azRise = (int16_t)lround(pinf.azstart);
      cand.azMax  = (int16_t)lround(pinf.azmax);
      cand.azSet  = (int16_t)lround(pinf.azstop);
      cand.elMax  = (int16_t)lround(pinf.maxelevation);
      cand.mag    = (float)mag;

      if (!refineToVisible(cand, cur.stdMag)) continue;
      if (cand.elMax < (int16_t)cfg.minElevDeg) continue;
      if (cand.mag > cfg.magLimit) continue;

      addPass(cand, cur);
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
       "zastaralych %u, nalezeno %u, %lu ms",
       nightHours, tried, skipped, stale, rt.passCount,
       (unsigned long)elapsed);

  savePasses();

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
