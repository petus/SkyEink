// portal.cpp
//
// Settings over WiFi. No OTA. The access point is secured; the password is
// derived from the MAC address, so it differs per device and stays the same
// across restarts.
//
// Every string goes through T(), the same as on the display, so switching the
// language changes both.

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <string.h>
#include "portal.h"
#include "config.h"
#include "screens.h"
#include "hal.h"
#include "i18n.h"
#include "log.h"
#include "version.h"

namespace portal {

namespace {

WebServer* srv = nullptr;
bool saved = false;

const char PAGE_CSS[] PROGMEM =
  "<style>"
  "body{font-family:system-ui,sans-serif;margin:0;padding:16px;max-width:560px;"
  "background:#f5f5f5;color:#111}"
  "h1{font-size:20px;margin:0 0 4px}"
  "p.sub{margin:0 0 20px;color:#666;font-size:13px}"
  "fieldset{border:1px solid #ccc;border-radius:6px;margin:0 0 16px;padding:12px}"
  "legend{font-size:13px;color:#666;padding:0 6px}"
  "label{display:block;font-size:13px;margin:10px 0 3px}"
  "input,select{width:100%;box-sizing:border-box;padding:8px;font-size:16px;"
  "border:1px solid #bbb;border-radius:4px;background:#fff}"
  "input[type=checkbox]{width:auto;margin-right:8px}"
  ".row{display:flex;gap:10px}.row>div{flex:1}"
  ".hint{font-size:12px;color:#777;margin:3px 0 0}"
  "button{width:100%;padding:13px;font-size:16px;border:0;border-radius:6px;"
  "background:#111;color:#fff;margin-top:6px}"
  "</style>";

// Shorthands, so that building the page stays readable.
void L(String& h, StrId id) {
  h += "<label>";
  h += T(id);
  h += "</label>";
}

void hint(String& h, StrId id) {
  h += "<p class=hint>";
  h += T(id);
  h += "</p>";
}

void legend(String& h, StrId id) {
  h += "<fieldset><legend>";
  h += T(id);
  h += "</legend>";
}

void checkbox(String& h, const char* name, bool on, StrId label) {
  h += "<label><input type=checkbox name=";
  h += name;
  h += " value=1";
  if (on) h += " checked";
  h += ">";
  h += T(label);
  h += "</label>";
}

void handleRoot() {
  String h = "<!DOCTYPE html><html lang=";
  h += (i18nGet() == LANG_EN) ? "en" : "cs";
  h += "><head><meta charset=utf-8>"
       "<meta name=viewport content='width=device-width,initial-scale=1'>"
       "<title>" FW_NAME "</title>";
  h += FPSTR(PAGE_CSS);
  h += "</head><body><h1>" FW_NAME " " FW_VERSION "</h1>"
       "<p class=sub>" FW_BOARD "</p><form method=POST action=/save>";

  // --- language first, so it is obvious where to switch it
  legend(h, W_LANG);
  h += "<select name=lang><option value=0";
  if (cfg.lang == LANG_CS) h += " selected";
  h += ">Čeština</option><option value=1";
  if (cfg.lang == LANG_EN) h += " selected";
  h += ">English</option></select></fieldset>";

  // --- WiFi
  legend(h, W_WIFI);
  L(h, W_SSID);
  h += "<input name=ssid maxlength=32 value='";
  h += cfg.ssid;
  h += "'>";
  L(h, W_PASS);
  h += "<input name=pass type=password maxlength=64 placeholder='";
  h += T(W_PASS_PH);
  h += "'></fieldset>";

  // --- site
  legend(h, W_SITE);
  h += "<div class=row><div>";
  L(h, W_LAT);
  h += "<input name=lat value='";
  h += String(cfg.lat, 4);
  h += "'></div><div>";
  L(h, W_LON);
  h += "<input name=lon value='";
  h += String(cfg.lon, 4);
  h += "'></div></div>";
  L(h, W_ALT);
  h += "<input name=alt value='";
  h += String(cfg.altM, 0);
  h += "'>";
  hint(h, W_SITE_HINT);

  // A zone preset plus a free text field. The free field wins, but an empty
  // one is ignored, so the zone can be fixed with a single tap.
  L(h, W_TZ);
  h += "<select name=tzsel>";
  static const struct { const char* tz; StrId label; } ZONES[] = {
    {"CET-1CEST,M3.5.0,M10.5.0/3",   W_TZ_PRAHA},
    {"EET-2EEST,M3.5.0/3,M10.5.0/4", W_TZ_ATHENS},
    {"GMT0BST,M3.5.0/1,M10.5.0",     W_TZ_LONDON},
    {"UTC0",                         W_TZ_UTC},
  };
  for (auto& z : ZONES) {
    h += "<option value='";
    h += z.tz;
    h += "'";
    if (strcmp(cfg.tz, z.tz) == 0) h += " selected";
    h += ">";
    h += T(z.label);
    h += "</option>";
  }
  h += "</select>";
  L(h, W_TZ_CUSTOM);
  h += "<input name=tz value='";
  h += cfg.tz;
  h += "'>";
  {
    char buf[192];
    snprintf(buf, sizeof(buf), T(W_TZ_HINT), hal::applyTimezone(cfg.tz) / 3600);
    h += "<p class=hint>";
    h += buf;
    h += "</p>";
  }
  h += "</fieldset>";

  // --- catalogue
  legend(h, W_CATALOG);
  checkbox(h, "g1", cfg.groups & GRP_VISUAL,   W_G_VISUAL);
  checkbox(h, "g2", cfg.groups & GRP_STATIONS, W_G_STATIONS);
  checkbox(h, "g4", cfg.groups & GRP_LAST30,   W_G_LAST30);
  checkbox(h, "g8", cfg.groups & GRP_STARLINK, W_G_STARLINK);
  hint(h, W_SL_HINT);
  L(h, W_SLMM);
  h += "<input name=slmm value='";
  h += String(cfg.slMinMeanMotion, 2);
  h += "'>";
  L(h, W_MAXOBJ);
  h += "<input name=maxobj value='";
  h += String(cfg.maxObjects);
  h += "'></fieldset>";

  // --- pass selection
  legend(h, W_SELECT);
  h += "<div class=row><div>";
  L(h, W_MAGLIM);
  h += "<input name=maglim value='";
  h += String(cfg.magLimit, 1);
  h += "'></div><div>";
  L(h, W_MINEL);
  h += "<input name=minel value='";
  h += String(cfg.minElevDeg);
  h += "'></div></div></fieldset>";

  // --- wake-ups
  legend(h, W_WAKE);
  checkbox(h, "ev", cfg.eventWakes, W_EVENTS);
  L(h, W_MAXEV);
  h += "<input name=maxev value='";
  h += String(cfg.maxEvents);
  h += "'>";
  h += "<div class=row><div>";
  L(h, W_QUIET_FROM);
  char mm[4];
  h += "<input name=qf value='";
  h += String(cfg.quietFromMin / 60);
  h += ":";
  snprintf(mm, sizeof(mm), "%02u", cfg.quietFromMin % 60);
  h += mm;
  h += "'></div><div>";
  L(h, W_QUIET_TO);
  h += "<input name=qt value='";
  h += String(cfg.quietToMin / 60);
  h += ":";
  snprintf(mm, sizeof(mm), "%02u", cfg.quietToMin % 60);
  h += mm;
  h += "'></div></div>";
  hint(h, W_QUIET_HINT);
  h += "</fieldset>";

  // The browser fills in its own clock. This is the last resort on networks
  // that block both NTP and outgoing HTTPS.
  h += "<input type=hidden name=now id=now>"
       "<button type=submit>";
  h += T(W_SAVE);
  h += "</button></form>"
       "<script>document.getElementById('now').value="
       "Math.floor(Date.now()/1000)</script></body></html>";

  srv->send(200, "text/html; charset=utf-8", h);
}

uint16_t parseHm(const String& s) {
  int c = s.indexOf(':');
  if (c < 0) return 0;
  int h = s.substring(0, c).toInt();
  int m = s.substring(c + 1).toInt();
  if (h < 0 || h > 23 || m < 0 || m > 59) return 0;
  return (uint16_t)(h * 60 + m);
}

void handleSave() {
  auto has = [&](const char* n) { return srv->hasArg(n); };
  auto arg = [&](const char* n) { return srv->arg(n); };

  if (has("lang")) {
    cfg.lang = (uint8_t)(arg("lang").toInt() ? LANG_EN : LANG_CS);
    i18nSet(cfg.lang);   // the reply below is already in the new language
  }

  if (has("ssid")) strncpy(cfg.ssid, arg("ssid").c_str(), sizeof(cfg.ssid) - 1);
  if (has("pass") && arg("pass").length())
    strncpy(cfg.pass, arg("pass").c_str(), sizeof(cfg.pass) - 1);

  // The preset first, then the free field when it is not empty.
  if (has("tzsel") && arg("tzsel").length())
    strncpy(cfg.tz, arg("tzsel").c_str(), sizeof(cfg.tz) - 1);
  if (has("tz") && arg("tz").length())
    strncpy(cfg.tz, arg("tz").c_str(), sizeof(cfg.tz) - 1);
  cfg.tz[sizeof(cfg.tz) - 1] = 0;
  LOGF("casove pasmo ulozeno '%s' -> posun %+ld h",
       cfg.tz, hal::applyTimezone(cfg.tz) / 3600);

  if (has("lat")) cfg.lat  = arg("lat").toFloat();
  if (has("lon")) cfg.lon  = arg("lon").toFloat();
  if (has("alt")) cfg.altM = arg("alt").toFloat();

  cfg.groups = 0;
  if (has("g1")) cfg.groups |= GRP_VISUAL;
  if (has("g2")) cfg.groups |= GRP_STATIONS;
  if (has("g4")) cfg.groups |= GRP_LAST30;
  if (has("g8")) cfg.groups |= GRP_STARLINK;

  if (has("slmm"))   cfg.slMinMeanMotion = arg("slmm").toFloat();
  if (has("maxobj")) cfg.maxObjects = (uint16_t)arg("maxobj").toInt();
  if (has("maglim")) cfg.magLimit = arg("maglim").toFloat();
  if (has("minel"))  cfg.minElevDeg = (uint8_t)arg("minel").toInt();

  cfg.eventWakes = has("ev");
  if (has("maxev")) cfg.maxEvents = (uint8_t)arg("maxev").toInt();
  if (has("qf")) cfg.quietFromMin = parseHm(arg("qf"));
  if (has("qt")) cfg.quietToMin   = parseHm(arg("qt"));

  // The browser clock. Only used when the RTC is not running or is off by
  // more than a minute, so saving settings never overwrites a more accurate
  // time from NTP.
  if (has("now")) {
    time_t browser = (time_t)arg("now").toInt();
    if (browser > 1735689600) {
      time_t cur = hal::timeNow();
      long diff = cur ? (long)(browser - cur) : 999999;
      if (diff < 0) diff = -diff;
      if (diff > 60) {
        hal::timeSet(browser);
        LOGF("cas nastaven z prohlizece, posun %ld s", diff);
      }
    }
  }

  bool ok = cfgSave();
  saved = ok;

  String r = "<!DOCTYPE html><html><head><meta charset=utf-8>"
             "<style>body{font-family:system-ui,sans-serif;padding:24px}</style>"
             "</head><body><h2>";
  r += T(ok ? W_SAVED : W_SAVE_FAIL);
  r += "</h2><p>";
  r += T(ok ? W_SAVED_TXT : W_FAIL_TXT);
  r += "</p></body></html>";
  srv->send(200, "text/html; charset=utf-8", r);
}

}  // namespace

bool run(uint16_t timeoutSec) {
  saved = false;

  // Password from the MAC address: different per unit, stable across
  // restarts.
  uint8_t mac[6];
  WiFi.macAddress(mac);
  char ssid[32], pw[16];
  snprintf(ssid, sizeof(ssid), "%s-%02X%02X", FW_NAME, mac[4], mac[5]);
  snprintf(pw, sizeof(pw), "sky%02X%02X%02X", mac[3], mac[4], mac[5]);

  WiFi.mode(WIFI_AP);
  WiFi.softAP(ssid, pw);
  delay(300);

  IPAddress ip = WiFi.softAPIP();
  char ips[20];
  snprintf(ips, sizeof(ips), "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);

  screens::portal(ssid, pw, ips);
  hal::dispFlushFull();

  srv = new WebServer(80);
  srv->on("/", handleRoot);
  srv->on("/save", HTTP_POST, handleSave);
  srv->onNotFound([]() { srv->send(404, "text/plain", "404"); });
  srv->begin();

  uint32_t t0 = millis();
  while (millis() - t0 < (uint32_t)timeoutSec * 1000) {
    srv->handleClient();
    if (saved) { delay(800); break; }
    delay(5);
  }

  srv->stop();
  delete srv;
  srv = nullptr;
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
  return saved;
}

}  // namespace portal
