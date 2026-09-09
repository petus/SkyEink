// astro.cpp
//
// Sun and Moon from abridged Meeus series, planets from Standish approximate
// elements (JPL, valid 1800 to 2050). All in double precision.

#include <math.h>
#include "astro.h"

namespace astro {

namespace {

const double DEG = M_PI / 180.0;
const double RAD = 180.0 / M_PI;

double norm360(double x) {
  x = fmod(x, 360.0);
  return x < 0 ? x + 360.0 : x;
}

double gmstFromJd(double jd);

// Greenwich sidereal time in degrees.
double gmstFromJd(double jd) {
  double t = (jd - 2451545.0) / 36525.0;
  double g = 280.46061837 + 360.98564736629 * (jd - 2451545.0)
           + 0.000387933 * t * t - t * t * t / 38710000.0;
  return norm360(g);
}

double obliquity(double jd) {
  double t = (jd - 2451545.0) / 36525.0;
  return 23.439291 - 0.0130042 * t;
}

// Equatorial to horizontal.
Horiz eqToHoriz(double raDeg, double decDeg, double jd,
                double latDeg, double lonDeg) {
  double lst = norm360(gmstFromJd(jd) + lonDeg);
  double ha  = norm360(lst - raDeg) * DEG;
  double dec = decDeg * DEG;
  double lat = latDeg * DEG;

  double sinEl = sin(dec) * sin(lat) + cos(dec) * cos(lat) * cos(ha);
  if (sinEl >  1.0) sinEl =  1.0;
  if (sinEl < -1.0) sinEl = -1.0;
  double el = asin(sinEl);

  double y = -cos(dec) * cos(lat) * sin(ha);
  double x =  sin(dec) - sin(lat) * sinEl;
  double az = atan2(y, x);

  Horiz h;
  h.az = norm360(az * RAD);
  h.el = el * RAD;
  return h;
}

// Ecliptic to equatorial.
void eclToEq(double lonDeg, double latDeg, double eps,
             double& raDeg, double& decDeg) {
  double l = lonDeg * DEG, b = latDeg * DEG, e = eps * DEG;
  double sl = sin(l), cl = cos(l), sb = sin(b), cb = cos(b);
  double ra  = atan2(sl * cos(e) - (sb / cb) * sin(e), cl);
  double dec = asin(sb * cos(e) + cb * sin(e) * sl);
  raDeg  = norm360(ra * RAD);
  decDeg = dec * RAD;
}

// Geocentric ecliptic longitude of the Sun and its distance in AU.
void sunEcl(double jd, double& lonDeg, double& rAu) {
  double n = jd - 2451545.0;
  double L = norm360(280.460 + 0.9856474 * n);
  double g = norm360(357.528 + 0.9856003 * n) * DEG;
  lonDeg = norm360(L + 1.915 * sin(g) + 0.020 * sin(2 * g));
  rAu    = 1.00014 - 0.01671 * cos(g) - 0.00014 * cos(2 * g);
}

void sunEq(double jd, double& raDeg, double& decDeg) {
  double lon, r;
  sunEcl(jd, lon, r);
  eclToEq(lon, 0.0, obliquity(jd), raDeg, decDeg);
}

// Moon, abridged Meeus. Main terms only, error within about 0.2 degrees.
void moonEcl(double jd, double& lonDeg, double& latDeg, double& distKm) {
  double t = (jd - 2451545.0) / 36525.0;

  double Lp = norm360(218.3164477 + 481267.88123421 * t);        // mean longitude
  double D  = norm360(297.8501921 + 445267.1114034  * t) * DEG;  // elongation
  double M  = norm360(357.5291092 +  35999.0502909  * t) * DEG;  // solar anomaly
  double Mp = norm360(134.9633964 + 477198.8675055  * t) * DEG;  // lunar anomaly
  double Fm = norm360( 93.2720950 + 483202.0175233  * t) * DEG;  // argument of latitude

  double dl = 6.288774 * sin(Mp)
            + 1.274027 * sin(2 * D - Mp)
            + 0.658314 * sin(2 * D)
            + 0.213618 * sin(2 * Mp)
            - 0.185116 * sin(M)
            - 0.114332 * sin(2 * Fm)
            + 0.058793 * sin(2 * D - 2 * Mp)
            + 0.057066 * sin(2 * D - M - Mp)
            + 0.053322 * sin(2 * D + Mp)
            + 0.045758 * sin(2 * D - M)
            - 0.040923 * sin(M - Mp)
            - 0.034720 * sin(D)
            - 0.030383 * sin(M + Mp);

  double db = 5.128122 * sin(Fm)
            + 0.280602 * sin(Mp + Fm)
            + 0.277693 * sin(Mp - Fm)
            + 0.173237 * sin(2 * D - Fm)
            + 0.055413 * sin(2 * D - Mp + Fm)
            + 0.046271 * sin(2 * D - Mp - Fm)
            + 0.032573 * sin(2 * D + Fm);

  double dr = -20905.355 * cos(Mp)
            -  3699.111 * cos(2 * D - Mp)
            -  2955.968 * cos(2 * D)
            -   569.925 * cos(2 * Mp)
            +    48.888 * cos(M)
            -     3.149 * cos(2 * Fm)
            +   246.158 * cos(2 * D - 2 * Mp)
            -   152.138 * cos(2 * D - M - Mp)
            -   170.733 * cos(2 * D + Mp)
            -   204.586 * cos(2 * D - M);

  lonDeg = norm360(Lp + dl);
  latDeg = db;
  distKm = 385000.56 + dr;
}

// Standish approximate planetary elements.
// a [AU], e, i [deg], L [deg], longitude of perihelion [deg], longitude of
// the ascending node [deg], and their rates per century.
struct Elem {
  double a, aD, e, eD, i, iD, L, LD, w, wD, om, omD;
};

const Elem ELEM[PL_COUNT + 1] = {
  // Mercury
  {0.38709927,  0.00000037, 0.20563593,  0.00001906,  7.00497902, -0.00594749,
   252.25032350, 149472.67411175, 77.45779628, 0.16047689, 48.33076593, -0.12534081},
  // Venus
  {0.72333566,  0.00000390, 0.00677672, -0.00004107,  3.39467605, -0.00078890,
   181.97909950,  58517.81538729,131.60246718, 0.00268329, 76.67984255, -0.27769418},
  // Mars
  {1.52371034,  0.00001847, 0.09339410,  0.00007882,  1.84969142, -0.00813131,
    -4.55343205,  19140.30268499,-23.94362959,0.44441088, 49.55953891, -0.29257343},
  // Jupiter
  {5.20288700, -0.00011607, 0.04838624, -0.00013253,  1.30439695, -0.00183714,
    34.39644051,   3034.74612775, 14.72847983, 0.21252668,100.47390909,  0.20469106},
  // Saturn
  {9.53667594, -0.00125060, 0.05386179, -0.00050991,  2.48599187,  0.00193609,
    49.95424423,   1222.49362201, 92.59887831,-0.41897216,113.66242448, -0.28867794},
  // Earth, last entry
  {1.00000261,  0.00000562, 0.01671123, -0.00004392, -0.00001531, -0.01294668,
   100.46457166,  35999.37244981,102.93768193, 0.32327364,  0.0,        0.0}
};

// Heliocentric ecliptic rectangular coordinates in AU.
void helioXYZ(const Elem& el, double t, double& x, double& y, double& z) {
  double a  = el.a  + el.aD  * t;
  double e  = el.e  + el.eD  * t;
  double I  = (el.i + el.iD  * t) * DEG;
  double L  = el.L  + el.LD  * t;
  double w  = el.w  + el.wD  * t;
  double om = el.om + el.omD * t;

  double M = norm360(L - w);
  if (M > 180.0) M -= 360.0;
  M *= DEG;

  // Kepler's equation, Newton iteration. A few steps suffice below e = 0.25.
  double E = M + e * sin(M);
  for (int k = 0; k < 6; k++) {
    double dE = (E - e * sin(E) - M) / (1.0 - e * cos(E));
    E -= dE;
    if (fabs(dE) < 1e-10) break;
  }

  double xv = a * (cos(E) - e);
  double yv = a * sqrt(1.0 - e * e) * sin(E);

  double wp = (w - om) * DEG;     // argument perihelia
  double on = om * DEG;

  double cw = cos(wp), sw = sin(wp);
  double co = cos(on), so = sin(on);
  double ci = cos(I),  si = sin(I);

  double xe = xv * cw - yv * sw;
  double ye = xv * sw + yv * cw;

  x = xe * co - ye * ci * so;
  y = xe * so + ye * ci * co;
  z = ye * si;
}

}  // namespace

const char* planetName(Planet p) {
  switch (p) {
    case PL_MERCURY: return "Merkur";
    case PL_VENUS:   return "Venuše";
    case PL_MARS:    return "Mars";
    case PL_JUPITER: return "Jupiter";
    case PL_SATURN:  return "Saturn";
    default:         return "?";
  }
}

double julianDay(time_t utc) {
  return (double)utc / 86400.0 + 2440587.5;
}

double gmstDeg(time_t utc) { return gmstFromJd(julianDay(utc)); }

time_t tmToUtc(const struct tm& t) {
  // Howard Hinnant's days_from_civil algorithm. No locale dependency and no
  // timegm(), which is missing on some toolchains.
  int y = t.tm_year + 1900;
  unsigned m = (unsigned)(t.tm_mon + 1);
  unsigned d = (unsigned)t.tm_mday;
  y -= m <= 2;
  const int era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = (unsigned)(y - era * 400);
  const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  long long days = (long long)era * 146097 + (long long)doe - 719468;
  return (time_t)(days * 86400LL + t.tm_hour * 3600LL + t.tm_min * 60LL + t.tm_sec);
}

Horiz sunAt(time_t utc, double latDeg, double lonDeg) {
  double jd = julianDay(utc), ra, dec;
  sunEq(jd, ra, dec);
  return eqToHoriz(ra, dec, jd, latDeg, lonDeg);
}

double sunElevation(time_t utc, double latDeg, double lonDeg) {
  return sunAt(utc, latDeg, lonDeg).el;
}

void sunUnitVector(time_t utc, double latDeg, double lonDeg,
                   double& e, double& n, double& u) {
  Horiz h = sunAt(utc, latDeg, lonDeg);
  double a = h.az * DEG, el = h.el * DEG;
  e = cos(el) * sin(a);
  n = cos(el) * cos(a);
  u = sin(el);
}

Horiz moonAt(time_t utc, double latDeg, double lonDeg) {
  double jd = julianDay(utc);
  double lon, lat, dist;
  moonEcl(jd, lon, lat, dist);
  double ra, dec;
  eclToEq(lon, lat, obliquity(jd), ra, dec);

  Horiz h = eqToHoriz(ra, dec, jd, latDeg, lonDeg);
  // Parallax. For the Moon it is almost a degree, so it cannot be skipped.
  double par = asin(6378.14 / dist) * RAD;
  h.el -= par * cos(h.el * DEG);
  return h;
}

double moonPhase(time_t utc, bool& waxing) {
  double jd = julianDay(utc);
  double mlon, mlat, mdist;
  moonEcl(jd, mlon, mlat, mdist);
  double slon, sr;
  sunEcl(jd, slon, sr);

  double elong = norm360(mlon - slon);
  waxing = elong < 180.0;
  // Illuminated fraction of the disc.
  return (1.0 - cos(elong * DEG)) / 2.0;
}

Horiz planetAt(Planet p, time_t utc, double latDeg, double lonDeg) {
  double jd = julianDay(utc);
  double t = (jd - 2451545.0) / 36525.0;

  double px, py, pz, ex, ey, ez;
  helioXYZ(ELEM[p], t, px, py, pz);
  helioXYZ(ELEM[PL_COUNT], t, ex, ey, ez);   // Earth

  double gx = px - ex, gy = py - ey, gz = pz - ez;
  double lon = norm360(atan2(gy, gx) * RAD);
  double lat = atan2(gz, sqrt(gx * gx + gy * gy)) * RAD;

  double ra, dec;
  eclToEq(lon, lat, obliquity(jd), ra, dec);
  return eqToHoriz(ra, dec, jd, latDeg, lonDeg);
}

double planetMag(Planet p, time_t utc) {
  double jd = julianDay(utc);
  double t = (jd - 2451545.0) / 36525.0;

  double px, py, pz, ex, ey, ez;
  helioXYZ(ELEM[p], t, px, py, pz);
  helioXYZ(ELEM[PL_COUNT], t, ex, ey, ez);

  double r = sqrt(px * px + py * py + pz * pz);          // od Slunce
  double gx = px - ex, gy = py - ey, gz = pz - ez;
  double d = sqrt(gx * gx + gy * gy + gz * gz);          // from Earth
  double s = sqrt(ex * ex + ey * ey + ez * ez);          // Earth from the Sun

  // Phase angle from the law of cosines.
  double cosPh = (r * r + d * d - s * s) / (2.0 * r * d);
  if (cosPh >  1.0) cosPh =  1.0;
  if (cosPh < -1.0) cosPh = -1.0;
  double ph = acos(cosPh) * RAD;

  // Simplified phase functions. Good enough for a label on a display.
  static const double H0[PL_COUNT] = {-0.36, -4.29, -1.52, -9.25, -8.88};
  double corr = 0.0;
  switch (p) {
    case PL_MERCURY: corr = 0.038 * ph - 0.000273 * ph * ph
                          + 0.000002 * ph * ph * ph; break;
    case PL_VENUS:   corr = 0.0009 * ph + 0.000239 * ph * ph
                          - 0.00000065 * ph * ph * ph; break;
    case PL_MARS:    corr = 0.016 * ph; break;
    case PL_JUPITER: corr = 0.005 * ph; break;
    case PL_SATURN:  corr = 0.044 * ph; break;   // rings not accounted for
    default: break;
  }
  return H0[p] + 5.0 * log10(r * d) + corr;
}

time_t sunCrossing(time_t from, double elevDeg, bool descending,
                   double latDeg, double lonDeg) {
  // Coarse five minute scan, then bisection down to one second.
  const int STEP = 300;
  double prev = sunElevation(from, latDeg, lonDeg);
  for (int i = 1; i <= 36 * 12; i++) {
    time_t t = from + (time_t)i * STEP;
    double cur = sunElevation(t, latDeg, lonDeg);

    bool hit = descending ? (prev > elevDeg && cur <= elevDeg)
                          : (prev < elevDeg && cur >= elevDeg);
    if (hit) {
      time_t lo = t - STEP, hi = t;
      while (hi - lo > 1) {
        time_t mid = lo + (hi - lo) / 2;
        double m = sunElevation(mid, latDeg, lonDeg);
        bool after = descending ? (m <= elevDeg) : (m >= elevDeg);
        if (after) hi = mid; else lo = mid;
      }
      return hi;
    }
    prev = cur;
  }
  return 0;
}

bool darkWindow(time_t around, double elevDeg,
                double latDeg, double lonDeg,
                time_t& start, time_t& end) {
  // Start 12 hours early so a dusk that has already passed is still found.
  time_t from = around - 12 * 3600;
  start = sunCrossing(from, elevDeg, true, latDeg, lonDeg);
  if (!start) return false;
  end = sunCrossing(start + 600, elevDeg, false, latDeg, lonDeg);
  if (!end) return false;

  // If that window has already ended, take the next one.
  if (end < around) {
    start = sunCrossing(end + 600, elevDeg, true, latDeg, lonDeg);
    if (!start) return false;
    end = sunCrossing(start + 600, elevDeg, false, latDeg, lonDeg);
    if (!end) return false;
  }
  return true;
}

}  // namespace astro
