// config.cpp

#include <Preferences.h>
#include <string.h>
#include "config.h"

SkyConfig cfg;

static Preferences prefs;
static const char* NS  = "skyeink";
static const char* KEY = "cfg";

void cfgDefaults(SkyConfig& c) {
  memset(&c, 0, sizeof(c));
  c.magic = CFG_MAGIC;
  c.ssid[0] = 0;
  c.pass[0] = 0;
  // Prague. Meant to be overwritten, but the device should show something
  // even before anyone configures it.
  c.lat  = 50.0755f;
  c.lon  = 14.4378f;
  c.altM = 200.0f;
  strncpy(c.tz, "CET-1CEST,M3.5.0,M10.5.0/3", sizeof(c.tz) - 1);

  c.groups      = GRP_VISUAL | GRP_STATIONS | GRP_LAST30;
  c.maxObjects  = 1200;
  c.magLimit    = 3.0f;
  c.minElevDeg  = 20;
  // Above 15.2 revolutions per day the orbit is below roughly 500 km. For
  // Starlink that picks out fresh trains, still raising and still bright.
  c.slMinMeanMotion = 15.2f;

  c.maxEvents   = 4;
  c.eventWakes  = true;
  c.quietFromMin = 0;
  c.quietToMin   = 0;      // equal values disable quiet hours

  c.sunSwitchDeg = 6;
  c.syncHourUtc  = 3;      // roughly 04:30 to 05:00 local time
  c.lang         = 0;      // Czech
}

static float clampf(float v, float lo, float hi) {
  return v < lo ? lo : (v > hi ? hi : v);
}

void cfgSanitize(SkyConfig& c) {
  c.ssid[sizeof(c.ssid) - 1] = 0;
  c.pass[sizeof(c.pass) - 1] = 0;
  c.tz[sizeof(c.tz) - 1] = 0;
  if (!c.tz[0]) strncpy(c.tz, "UTC0", sizeof(c.tz) - 1);

  c.lat  = clampf(c.lat, -90.0f, 90.0f);
  c.lon  = clampf(c.lon, -180.0f, 180.0f);
  c.altM = clampf(c.altM, -400.0f, 5000.0f);

  if (!c.groups) c.groups = GRP_VISUAL | GRP_STATIONS;
  if (c.maxObjects < 50)   c.maxObjects = 50;
  if (c.maxObjects > 4000) c.maxObjects = 4000;
  c.magLimit = clampf(c.magLimit, -4.0f, 6.0f);
  if (c.minElevDeg < 5)  c.minElevDeg = 5;
  if (c.minElevDeg > 60) c.minElevDeg = 60;
  c.slMinMeanMotion = clampf(c.slMinMeanMotion, 13.0f, 16.5f);

  if (c.maxEvents > 8) c.maxEvents = 8;
  if (c.quietFromMin > 1439) c.quietFromMin = 0;
  if (c.quietToMin   > 1439) c.quietToMin   = 0;

  if (c.sunSwitchDeg < 1)  c.sunSwitchDeg = 1;
  if (c.sunSwitchDeg > 18) c.sunSwitchDeg = 18;
  if (c.syncHourUtc > 23)  c.syncHourUtc = 3;
  if (c.lang > 1) c.lang = 0;
}

bool cfgLoad() {
  cfgDefaults(cfg);
  if (!prefs.begin(NS, true)) return false;

  SkyConfig tmp;
  size_t n = prefs.getBytes(KEY, &tmp, sizeof(tmp));
  prefs.end();

  if (n != sizeof(tmp) || tmp.magic != CFG_MAGIC) return false;
  cfg = tmp;
  cfgSanitize(cfg);
  return true;
}

bool cfgSave() {
  cfgSanitize(cfg);
  cfg.magic = CFG_MAGIC;

  if (!prefs.begin(NS, false)) return false;
  size_t w = prefs.putBytes(KEY, &cfg, sizeof(cfg));
  prefs.end();
  if (w != sizeof(cfg)) return false;

  // Read back and compare. A write to NVS on nearly full flash can fail
  // silently.
  SkyConfig back;
  if (!prefs.begin(NS, true)) return false;
  size_t r = prefs.getBytes(KEY, &back, sizeof(back));
  prefs.end();
  return r == sizeof(back) && memcmp(&back, &cfg, sizeof(cfg)) == 0;
}

void cfgErase() {
  if (prefs.begin(NS, false)) {
    prefs.clear();
    prefs.end();
  }
  cfgDefaults(cfg);
}

bool cfgHasWifi() { return cfg.ssid[0] != 0; }
