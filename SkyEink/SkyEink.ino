// =============================================================================
//  SkyEink - satellite passes, cloud cover and planets on e-paper
// =============================================================================
//
//  A battery powered e-paper display for night sky observing. It predicts
//  which satellites will actually be visible tonight, shows the hourly cloud
//  cover you need in order to see them, and plots the planets and the Moon.
//  The display only refreshes when it has something new to say, so a single
//  charge lasts for months.
//
//  Created by chiptron.cz with Claude AI.
//  https://chiptron.cz
//
// -----------------------------------------------------------------------------
//  HARDWARE
// -----------------------------------------------------------------------------
//
//  Soldered Inkplate 5 Gen2. ESP32-WROVER, 720x1280 e-paper panel with 8 grey
//  levels, PCF85063A RTC with a backup cell, microSD slot and a LiPo charger.
//
//    WAKE button    GPIO36    RTC domain, ext0 capable
//    RTC interrupt  GPIO39    RTC domain, currently unused
//
//  All board specific code lives in board_inkplate5v2.cpp. Porting to another
//  panel means writing one more board file and flipping a define in
//  board_config.h.
//
// -----------------------------------------------------------------------------
//  WHAT IT DOES
// -----------------------------------------------------------------------------
//
//  * Predicts visible satellite passes for the coming night from the Celestrak
//    catalogue. A pass makes the list only if the satellite is high enough,
//    lit by the Sun while the observer is in the dark, and bright enough.
//  * Shows hourly cloud cover, temperature and precipitation from Open-Meteo.
//  * Computes the Sun, the Moon and five planets locally, so that part of the
//    screen keeps working with no network at all.
//  * Switches between the day and night screens on its own, driven by solar
//    elevation. No user interaction needed.
//  * Wakes up five minutes before each bright pass and shows a countdown with
//    the ground track drawn on a polar plot.
//  * User interface in Czech or English, selectable in the settings.
//
// -----------------------------------------------------------------------------
//  CONFIGURATION
// -----------------------------------------------------------------------------
//
//  The board has a single user button, so actions are told apart by how long
//  it is held:
//
//    short press      recompute, with a progress screen
//    hold for 5 s     start the configuration access point
//
//  In access point mode the display shows the SSID, the password and a QR
//  code. Join the network and open http://192.168.4.1 in a browser. The
//  password is derived from the MAC address, so it differs per device and
//  stays the same across restarts. The access point shuts down after five
//  minutes.
//
//  Set the site coordinates first. Pass prediction needs them accurate to
//  within a kilometre.
//
//  There is no OTA. Firmware is flashed over USB.
//
// -----------------------------------------------------------------------------
//  LIBRARIES
// -----------------------------------------------------------------------------
//
//    InkplateLibrary   Soldered              LGPL-3.0
//      bundles Adafruit GFX                  BSD 3-clause
//      bundles SdFat                         MIT
//    Sgp4              Hopperpop             MIT
//    ArduinoJson       Benoit Blanchon       MIT
//    qrcode            Espressif, in the core  Apache-2.0
//    arduino-esp32     Espressif             LGPL-2.1
//
//  The FreeSans fonts in Fonts/ come from Adafruit GFX and are derived from
//  GNU FreeFont, released under the GPL with the font exception. That
//  exception explicitly allows embedding the fonts in a program without the
//  program itself becoming GPL.
//
// -----------------------------------------------------------------------------
//  DATA SOURCES
// -----------------------------------------------------------------------------
//
//  Celestrak      orbital elements. Free to use, but the service enforces one
//                 download per data update. This firmware fetches at most once
//                 a day and backs off on HTTP 403.
//                 https://celestrak.org
//
//  Open-Meteo     weather. Data is licensed CC BY 4.0 and free for
//                 non-commercial use. Attribution belongs in anything you
//                 publish from it.
//                 https://open-meteo.com
//
// -----------------------------------------------------------------------------
//  LICENSE
// -----------------------------------------------------------------------------
//
//  MIT License
//
//  Copyright (c) 2026 chiptron.cz
//
//  Permission is hereby granted, free of charge, to any person obtaining a
//  copy of this software and associated documentation files (the "Software"),
//  to deal in the Software without restriction, including without limitation
//  the rights to use, copy, modify, merge, publish, distribute, sublicense,
//  and/or sell copies of the Software, and to permit persons to whom the
//  Software is furnished to do so, subject to the following conditions:
//
//  The above copyright notice and this permission notice shall be included in
//  all copies or substantial portions of the Software.
//
//  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
//  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
//  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
//  THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
//  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
//  FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
//  DEALINGS IN THE SOFTWARE.
//
//  MIT is compatible with every library above. InkplateLibrary is LGPL-3.0,
//  which allows use from a differently licensed program as long as users can
//  replace the library and relink. Publishing the full source of this sketch
//  satisfies that.
//
// -----------------------------------------------------------------------------
//
//  Everything happens in setup(). The device wakes, decides what to do,
//  redraws the display and goes back to sleep. loop() never runs.
//
// =============================================================================

#include <Arduino.h>
#include <time.h>

#include "board_config.h"
#include "version.h"
#include "hal.h"
#include "layout.h"
#include "config.h"
#include "runtime.h"
#include "astro.h"
#include "gp.h"
#include "passes.h"
#include "weather.h"
#include "screens.h"
#include "ui.h"
#include "portal.h"
#include "i18n.h"
#include "log.h"

// How many minutes before rise the device wakes for a pass.
#define LEAD_MIN 5
// Passes closer together than this share a single wake-up.
#define MERGE_MIN 15
// How long after set the device returns to the normal overview.
#define PASS_TAIL_S 60
// Below this voltage the per-pass wake-ups are disabled.
#define BATT_LOW 3.50f
// Below this, writing to flash is not allowed.
#define BATT_CRITICAL 3.30f

namespace {

void onProgress(const char* stage, uint8_t pct) {
  screens::progress(stage, pct);
  hal::dispFlushPartial();
}

void showProgress(const char* stage, uint8_t pct) {
  hal::dispSetMode(hal::MODE_MONO);
  hal::dispClear();
  layout::Frame f;
  layout::buildFrame(f);
  ui::drawHeader(f, T(S_RECOMPUTE), hal::timeNow());
  screens::progress(stage, pct);
  hal::dispFlushFull();
}

// Tries every route to the current time in turn and logs which one worked.
// Some ISPs block UDP port 123, which is what NTP runs on. The Date header
// of an HTTPS response gets through wherever the data itself does.
bool syncTime() {
  if (hal::netSyncTime("tik.cesnet.cz", "pool.ntp.org", "time.google.com", 8000)) {
    LOGS("cas: NTP ok");
    rt.lastNtpAt = hal::timeNow();
    return true;
  }
  LOGS("cas: NTP selhalo, zkousim hlavicku Date pres HTTPS");

  static const char* URLS[] = {
    "https://api.open-meteo.com/v1/forecast?latitude=0&longitude=0",
    "https://celestrak.org/",
    "https://www.google.com/"
  };
  for (const char* u : URLS) {
    if (hal::netSyncTimeHttp(u, 8000)) {
      LOGF("cas: Date ok z %s", u);
      rt.lastNtpAt = hal::timeNow();
      return true;
    }
  }
  LOGS("cas: vsechny cesty selhaly");
  return false;
}

bool inQuiet(time_t t) {
  if (cfg.quietFromMin == cfg.quietToMin) return false;
  struct tm lt;
  localtime_r(&t, &lt);
  uint16_t m = (uint16_t)(lt.tm_hour * 60 + lt.tm_min);
  if (cfg.quietFromMin < cfg.quietToMin)
    return m >= cfg.quietFromMin && m < cfg.quietToMin;
  return m >= cfg.quietFromMin || m < cfg.quietToMin;   // wraps past midnight
}

// Fetches whatever is stale and recomputes the passes. True on success.
bool fullSync(time_t now, bool withGp) {
  showProgress(T(S_P_WIFI), 2);

  if (!hal::netConnect(cfg.ssid, cfg.pass, 20000)) {
    rt.netFailStreak++;
    LOGF("WiFi selhala (%u. neuspech v rade)", rt.netFailStreak);
    return false;
  }
  LOGF("WiFi pripojena, RSSI %d dBm", hal::netRssi());

  // The clock is synced on every full fetch, not only when it is missing.
  // Any plausible looking time used to be good enough, so a clock that was
  // hours off would never get corrected.
  onProgress(T(S_P_TIME), 5);
  time_t before = hal::timeNow();
  if (syncTime()) {
    now = hal::timeNow();
    long jump = (long)(now - before);
    if (before && (jump > 120 || jump < -120))
      LOGF("POZOR: hodiny se posunuly o %ld s (%.2f h)", jump, jump / 3600.0);
  }

  bool gpOk = !withGp;
  if (withGp) {
    int n = gp::refresh(onProgress);
    const char* why = "ok";
    if (n == GP_ERR_403)        why = "data se nezmenila, HTTP 403";
    else if (n == GP_ERR_STORE) why = "uloziste chybi nebo je plne";
    else if (n < 0)             why = "chyba site";
    LOGF("Celestrak vratil %d (%s)", n, why);

    rt.storeFull = (n == GP_ERR_STORE);
    if (rt.storeFull)
      LOGF("uloziste %s, volno %ld kB, potreba %lu kB",
           hal::storeName(), (long)hal::storeFreeKb(),
           (unsigned long)(gp::cacheBytesNeeded() * 2 / 1024));

    if (n > 0) {
      rt.gpFetchedAt   = now;
      rt.gpCount       = (uint16_t)n;
      rt.gpEpochOldest = gp::oldestEpoch();
      gpOk = true;
    } else if (n == GP_ERR_403) {
      // Celestrak says the data has not changed since last time. Keep the
      // existing cache and do not ask again today.
      gpOk = rt.gpCount > 0;
    }
  }

  onProgress(T(S_P_FORECAST), 92);
  bool wx = weather::fetch(now, SKY_MAX_HOURS);
  if (wx) rt.cloudFetchedAt = now;
  int8_t tlo = 0, thi = 0;
  bool haveT = weather::tempRange(tlo, thi);
  LOGF("pocasi: %s, %u hodin, teplota %d az %d C, srazky do %u %%",
       wx ? "ok" : "chyba", rt.cloud.hours,
       haveT ? tlo : 0, haveT ? thi : 0, weather::maxRain());

  hal::netDisconnect();
  rt.lastNetOkAt = now;
  rt.netFailStreak = 0;
  return gpOk;
}

void recompute(time_t now) {
  showProgress(T(S_P_PASSES), 8);
  passes::compute(now, onProgress);
}

// Works out the next wake-up time. Called before drawing, because that time
// is shown in the screen header.
time_t computeNextWake(time_t now) {
  time_t next = now + 3600;

  // 1. Day/night transition, driven by solar elevation.
  double el = astro::sunElevation(now, cfg.lat, cfg.lon);
  bool night = el < -(double)cfg.sunSwitchDeg;
  time_t sw = astro::sunCrossing(now + 60, -(double)cfg.sunSwitchDeg,
                                 night ? false : true, cfg.lat, cfg.lon);
  if (sw && sw < next) next = sw;

  // 2. Morning sync.
  struct tm g;
  gmtime_r(&now, &g);
  g.tm_hour = cfg.syncHourUtc; g.tm_min = 0; g.tm_sec = 0;
  time_t sync = astro::tmToUtc(g);
  if (sync <= now) sync += 86400;
  if (sync < next) next = sync;

  // 3. Passes. Each one needs three moments, not one:
  //      rise minus five minutes  -> detail screen with a countdown
  //      rise                     -> detail switches to "in progress"
  //      set plus one minute      -> back to the normal overview
  //    Without the last one the detail screen would sit there until the next
  //    hourly wake-up, easily forty minutes after the pass had ended.
  float v = hal::batteryVolts();
  bool allowEvents = cfg.eventWakes && (v != v || v > BATT_LOW);
  if (allowEvents) {
    uint8_t used = 0;
    for (uint8_t i = 0; i < rt.passCount; i++) {
      PassRec& p = rt.passes[i];

      // A pass that has already been announced still needs a wake-up for
      // its own start and one to return to the overview.
      if (p.notified) {
        used++;
        if (p.tRise > now && p.tRise < next) next = p.tRise;
        time_t back = p.tSet + PASS_TAIL_S;
        if (back > now && back < next) next = back;
        continue;
      }

      time_t at = p.tRise - (time_t)LEAD_MIN * 60;
      if (at <= now) continue;
      if (used >= cfg.maxEvents) break;
      if (inQuiet(at)) continue;
      if (at < next) next = at;
      break;
    }
  }

  if (next <= now + 30) next = now + 30;
  return next;
}

// Sleeps until the time computed earlier. Does not return.
[[noreturn]] void sleepNow(time_t now) {
  time_t next = rt.nextWakeAt > now ? rt.nextWakeAt : now + 60;
  struct tm lt;
  localtime_r(&next, &lt);
  LOGF("dalsi probuzeni %02d:%02d:%02d mistniho casu, za %ld s",
       lt.tm_hour, lt.tm_min, lt.tm_sec, (long)(next - now));
  LOGSEP();
  Serial.flush();
  hal::idleUntil(next);
}

// Which pass should be on screen right now, and whether another one is
// close enough to share the screen.
int currentPass(time_t now, uint8_t& also) {
  also = 0xFF;
  for (uint8_t i = 0; i < rt.passCount; i++) {
    PassRec& p = rt.passes[i];
    time_t from = p.tRise - (time_t)LEAD_MIN * 60;
    if (now >= from && now <= p.tSet) {
      // Merge with the next one if it follows immediately.
      if (i + 1 < rt.passCount &&
          rt.passes[i + 1].tRise - p.tSet < (time_t)MERGE_MIN * 60)
        also = (uint8_t)(i + 1);
      return (int)i;
    }
  }
  return -1;
}

}  // namespace

void setup() {
  hal::boardEarlyInit();

  // The RTC RAM state is checked first. It needs no peripherals and the log
  // line right below depends on it.
  bool coldStart = !rtValid();
  if (coldStart) rtInit();
  rt.bootCount++;

  LOGSEP();
  LOGF("%s v%s  %s", FW_NAME, FW_VERSION, FW_BOARD);
  static const char* WR[] = {"zapnuti", "casovac", "tlacitko", "rtc", "?"};
  LOGF("probuzeni: %s", WR[hal::wakeReason() <= hal::WAKE_UNKNOWN
                            ? hal::wakeReason() : hal::WAKE_UNKNOWN]);
  LOGF("start c. %lu, %s", (unsigned long)rt.bootCount,
       coldStart ? "studeny (RTC RAM byla prazdna)" : "z deep sleepu");

  hal::dispBegin();
  LOGF("panel %dx%d, %d urovni, heap %lu B, psram %lu B",
       hal::dispCaps().w, hal::dispCaps().h, hal::dispCaps().grayLevels,
       (unsigned long)hal::sysFreeHeap(), (unsigned long)hal::sysFreePsram());

  bool fresh = !cfgLoad();
  i18nSet(cfg.lang);
  long tzOff = hal::applyTimezone(cfg.tz);
  LOGF("casove pasmo '%s' -> %s, posun %+ld h",
       cfg.tz, hal::timezoneName(), tzOff / 3600);
  LOGF("konfigurace: %s, sit '%s', pozice %.4f %.4f",
       fresh ? "vychozi" : "nactena", cfg.ssid, (double)cfg.lat, (double)cfg.lon);
  LOGF("SD karta: %s", hal::storeBegin() ? "ok" : "CHYBI");

  // Button: a short press recomputes, a long press opens the settings. The
  // board has a single WAKE button, so the two are told apart by hold time.
  bool wantPortal = false, wantRecompute = false;
  if (hal::wakeReason() == hal::WAKE_BUTTON || hal::wakeReason() == hal::WAKE_POWERON) {
    uint32_t held = hal::buttonHeldMs(hal::BTN_A);
    if (held >= 5000)      wantPortal = true;
    else if (held >= 50)   wantRecompute = true;
  }

  if (fresh || !cfgHasWifi()) wantPortal = true;

  if (wantPortal) {
    LOGS("spoustim konfiguracni pristupovy bod");
    portal::run(300);
    hal::sysRestart();
  }

  time_t now = hal::timeNow();
  if (now && rt.lastBootAt)
    LOGF("od minuleho probuzeni uplynulo %ld s", (long)(now - rt.lastBootAt));
  if (now) rt.lastBootAt = now;

  {
    struct tm lt;
    if (now) {
      struct tm ut;
      gmtime_r(&now, &ut);
      localtime_r(&now, &lt);
      uint32_t err = hal::timeErrorEstimateSec();
      char errs[24];
      if (err == 0xFFFFFFFFUL) strcpy(errs, "neznama");
      else snprintf(errs, sizeof(errs), "do %lu s", (unsigned long)err);
      LOGF("cas UTC %02d:%02d:%02d, mistni %02d:%02d:%02d (%s), chyba %s",
           ut.tm_hour, ut.tm_min, ut.tm_sec,
           lt.tm_hour, lt.tm_min, lt.tm_sec, hal::timezoneName(), errs);
    } else LOGS("cas neni platny");
  }

  // Nothing can be computed without a valid clock.
  if (!now) {
    showProgress(T(S_P_WIFI), 2);
    bool ok = hal::netConnect(cfg.ssid, cfg.pass, 20000) && syncTime();
    hal::netDisconnect();
    if (ok) {
      now = hal::timeNow();
    } else {
      rt.nextWakeAt = 0;
      screens::status(0, T(S_FAULT_TIME));
      hal::dispFlushFull();
      hal::idleUntil(0);   // za hodinu znovu
    }
  }

  float volts = hal::batteryVolts();
  bool battCritical = (volts == volts) && volts < BATT_CRITICAL;
  LOGF("baterie %.2f V, %u %%%s", (double)volts, ui::battPercent(volts),
       battCritical ? "  KRITICKY NIZKA" : "");

  // What needs refreshing.
  // A negative age means the clock jumped backwards. The data then looked
  // fresh, the device stopped fetching entirely, and the clock could never
  // be corrected. Negative ages therefore force a refresh.
  long gpAge = (long)(now - rt.gpFetchedAt);
  long wxAge = (long)(now - rt.cloudFetchedAt);
  bool needGp      = !rt.gpFetchedAt || gpAge > 20 * 3600 || gpAge < 0;
  bool needWeather = !rt.cloudFetchedAt || wxAge > 6 * 3600 || wxAge < 0;
  bool needPasses  = !rt.passesComputedAt || now > rt.passesValidTo ||
                     now < rt.passesComputedAt;

  // Once the last pass in the list is over there is nothing left to show,
  // whatever the validity window says. Recompute for the next night.
  if (!needPasses && rt.passCount &&
      rt.passes[rt.passCount - 1].tSet < now) needPasses = true;

  if (wantRecompute) { needGp = true; needWeather = true; needPasses = true; }
  LOGF("potreba: elementy=%d pocasi=%d prelety=%d (v cache %u objektu, %u preletu)",
       needGp, needWeather, needPasses, rt.gpCount, rt.passCount);

  if ((needGp || needWeather) && !battCritical) {
    if (fullSync(now, needGp)) needPasses = true;
    now = hal::timeNow();
  }

  if (needPasses && rt.gpCount) {
    uint32_t t0 = millis();
    recompute(now);
    now = hal::timeNow();
    LOGF("vypocet hotov za %lu ms, nalezeno %u preletu",
         (unsigned long)(millis() - t0), rt.passCount);
    {
      struct tm a, b;
      time_t va = rt.passesComputedAt, vb = rt.passesValidTo;
      localtime_r(&va, &a);
      localtime_r(&vb, &b);
      LOGF("seznam plati do %02d:%02d, pocitano v %02d:%02d mistniho casu",
           b.tm_hour, b.tm_min, a.tm_hour, a.tm_min);
    }
    for (uint8_t i = 0; i < rt.passCount; i++) {
      const PassRec& p = rt.passes[i];
      struct tm lt; localtime_r(&p.tRise, &lt);
      LOGF("  %02d:%02d  %-16s max %2d deg  mag %+.1f  %ld min",
           lt.tm_hour, lt.tm_min, p.name, p.elMax, (double)p.mag,
           (long)(p.tSet - p.tRise) / 60);
    }
  }

  // Faults take precedence over content.
  const char* fault = nullptr;
  long gpAgeDays = rt.gpEpochOldest ? (long)(now - rt.gpEpochOldest) / 86400 : 0;
  if (rt.storeFull)           fault = T(S_FAULT_STORE);
  else if (rt.netFailStreak >= 2) fault = T(S_FAULT_WIFI);
  else if (gpAgeDays > 14)    fault = T(S_FAULT_OLD);
  else if (battCritical)      fault = T(S_FAULT_BATT);

  if (fault) {
    LOGF("PORUCHA: %s", fault);
    rt.nextWakeAt = computeNextWake(now);
    screens::status(now, fault);
    hal::dispFlushFull();
    sleepNow(now);
  }

  // Screen selection. Automatic, no user interaction.
  uint8_t also;
  int pidx = currentPass(now, also);
  double sunEl = astro::sunElevation(now, cfg.lat, cfg.lon);

  // The notified flag has to be set before scheduling, otherwise the device
  // would schedule itself for the same pass again.
  if (pidx >= 0 && cfg.eventWakes) {
    rt.passes[pidx].notified = true;
    if (also < rt.passCount) rt.passes[also].notified = true;
  }

  rt.nextWakeAt = computeNextWake(now);

  if (pidx >= 0 && cfg.eventWakes) {
    screens::pass(now, (uint8_t)pidx, also);
    rt.lastScreen = SCR_PASS;
  } else if (sunEl < -(double)cfg.sunSwitchDeg) {
    screens::night(now);
    rt.lastScreen = SCR_NIGHT;
  } else {
    screens::day(now);
    rt.lastScreen = SCR_DAY;
  }

  LOGF("obrazovka: %s",
       rt.lastScreen == SCR_PASS ? "prelet"
       : rt.lastScreen == SCR_NIGHT ? "noc" : "den");
  hal::dispFlushFull();
  sleepNow(now);
}

void loop() {
  // Never reached. setup() ends by going to sleep.
}
