// weather.cpp
//
// The Open-Meteo response is downloaded into a buffer and only then parsed
// with ArduinoJson using a filter. The filter keeps just the listed keys in
// the document and discards the rest of the response on the way through.
//
// Why not parse straight from the stream: ArduinoJson reads via readBytes(),
// which obeys the Stream timeout, but WiFiClient::setTimeout() takes seconds
// in some ESP32 core versions and milliseconds in others. The parser can
// therefore stop halfway and return IncompleteInput. The chunked download is
// proven and already used by the element module, so both take the same route.
//
// start_hour and end_hour bound the query, so the response stays under two
// kilobytes and fits in memory comfortably.

#include <Arduino.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <ArduinoJson.h>
#include "weather.h"
#include "hal.h"
#include "config.h"
#include "runtime.h"
#include "astro.h"
#include "log.h"

namespace weather {

namespace {

// Headroom. Sixteen hours times five quantities comes to about 1.5 kB; eight
// kilobytes covers the server adding a field or changing its formatting.
const size_t BUF_CAP = 8192;

struct Buf {
  char*  p;
  size_t len;
  bool   over;
};

bool onChunk(const uint8_t* data, size_t n, void* ctx) {
  Buf* b = (Buf*)ctx;
  if (b->len + n + 1 > BUF_CAP) { b->over = true; return false; }
  memcpy(b->p + b->len, data, n);
  b->len += n;
  return true;
}

time_t parseIso(const char* s) {
  if (!s) return 0;
  int Y, Mo, D, H, Mi;
  if (sscanf(s, "%d-%d-%dT%d:%d", &Y, &Mo, &D, &H, &Mi) < 5) return 0;
  struct tm t = {};
  t.tm_year = Y - 1900; t.tm_mon = Mo - 1; t.tm_mday = D;
  t.tm_hour = H; t.tm_min = Mi;
  return astro::tmToUtc(t);
}

void isoHour(char* out, size_t n, time_t t) {
  struct tm g;
  gmtime_r(&t, &g);
  snprintf(out, n, "%04d-%02d-%02dT%02d:00",
           g.tm_year + 1900, g.tm_mon + 1, g.tm_mday, g.tm_hour);
}

bool parseInto(const char* json, size_t len, time_t want) {
#if ARDUINOJSON_VERSION_MAJOR >= 7
  JsonDocument filter;
  JsonDocument doc;
#else
  DynamicJsonDocument filter(256);
  DynamicJsonDocument doc(12288);
#endif
  filter["hourly"]["time"] = true;
  filter["hourly"]["cloud_cover"] = true;
  filter["hourly"]["temperature_2m"] = true;
  filter["hourly"]["precipitation_probability"] = true;
  filter["hourly"]["precipitation"] = true;

  DeserializationError err =
      deserializeJson(doc, json, len, DeserializationOption::Filter(filter));
  if (err) {
    LOGF("pocasi: JSON %s", err.c_str());
    return false;
  }

  JsonArrayConst times = doc["hourly"]["time"].as<JsonArrayConst>();
  JsonArrayConst cloud = doc["hourly"]["cloud_cover"].as<JsonArrayConst>();
  JsonArrayConst temp  = doc["hourly"]["temperature_2m"].as<JsonArrayConst>();
  JsonArrayConst prob  = doc["hourly"]["precipitation_probability"].as<JsonArrayConst>();
  JsonArrayConst mm    = doc["hourly"]["precipitation"].as<JsonArrayConst>();

  if (times.isNull() || cloud.isNull()) {
    LOGS("pocasi: v odpovedi chybi pole hourly");
    return false;
  }

  // The first slot that falls inside our window. With start_hour it is
  // usually zero, but we do not rely on that.
  size_t off = 0;
  bool found = false;
  for (size_t i = 0; i < times.size(); i++) {
    time_t t = parseIso(times[i].as<const char*>());
    if (t && t >= want) { off = i; found = true; break; }
  }
  if (!found) {
    LOGS("pocasi: zadny slot nespada do pozadovaneho okna");
    return false;
  }

  rt.cloud.t0 = parseIso(times[off].as<const char*>());
  rt.cloud.hours = 0;

  for (size_t i = off; i < times.size() && rt.cloud.hours < SKY_MAX_HOURS; i++) {
    const uint8_t k = rt.cloud.hours;

    rt.cloud.total[k] = (uint8_t)(i < cloud.size() ? cloud[i].as<int>() : 0);

    // Temperature and precipitation are optional. If the server omits them
    // or sends null, the rest of the screen keeps working.
    if (i < temp.size() && !temp[i].isNull())
      rt.cloud.temp[k] = (int8_t)lroundf(temp[i].as<float>());
    else
      rt.cloud.temp[k] = -128;

    rt.cloud.rain[k] = (uint8_t)((i < prob.size() && !prob[i].isNull())
                                 ? prob[i].as<int>() : 0);

    long t10 = (i < mm.size() && !mm[i].isNull())
               ? lroundf(mm[i].as<float>() * 10.0f) : 0;
    if (t10 < 0) t10 = 0;
    if (t10 > 255) t10 = 255;
    rt.cloud.mm10[k] = (uint8_t)t10;

    rt.cloud.hours++;
  }

  return rt.cloud.hours > 0;
}

}  // namespace

bool fetch(time_t from, uint8_t hours) {
  if (!hours || hours > SKY_MAX_HOURS) hours = SKY_MAX_HOURS;

  // Round down to the whole hour so the slots line up with the labels.
  time_t start = from - (from % 3600);
  time_t stop  = start + (time_t)(hours - 1) * 3600;

  char h0[24], h1[24];
  isoHour(h0, sizeof(h0), start);
  isoHour(h1, sizeof(h1), stop);

  char url[288];
  snprintf(url, sizeof(url),
           "https://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f"
           "&hourly=cloud_cover,temperature_2m,precipitation_probability,"
           "precipitation&timezone=UTC&start_hour=%s&end_hour=%s",
           (double)cfg.lat, (double)cfg.lon, h0, h1);

  Buf b;
  b.p = (char*)malloc(BUF_CAP);
  if (!b.p) { LOGS("pocasi: nedostatek pameti na buffer"); return false; }
  b.len = 0;
  b.over = false;

  int rc = hal::netGet(url, onChunk, &b, 15000);
  b.p[b.len] = 0;

  LOGF("pocasi: HTTP %d, %u B, volna halda %lu B", rc, (unsigned)b.len,
       (unsigned long)hal::sysFreeHeap());

  bool ok = false;
  if (b.over) {
    LOGF("pocasi: odpoved presahla %u B", (unsigned)BUF_CAP);
  } else if (rc != 200) {
    LOGF("pocasi: server odmitl, HTTP %d", rc);
  } else if (b.len == 0) {
    LOGS("pocasi: prazdna odpoved");
  } else {
    // A quick check that this is JSON at all. If the server returned an
    // error page or compressed data, we find out here and the log says why,
    // instead of a mysteriously missing field later on.
    const char* q = b.p;
    while (*q == ' ' || *q == '\r' || *q == '\n' || *q == '\t') q++;
    if (*q != '{') {
      char head[41];
      snprintf(head, sizeof(head), "%.40s", q);
      LOGF("pocasi: odpoved neni JSON, zacina '%s'", head);
    } else {
      ok = parseInto(b.p, b.len, start);
    }
  }

  free(b.p);
  return ok;
}

int slotFor(time_t t) {
  if (!rt.cloud.hours) return -1;
  long d = (long)(t - rt.cloud.t0);
  if (d < 0) return -1;
  int i = (int)(d / 3600);
  return i < rt.cloud.hours ? i : -1;
}

uint16_t ageHours(time_t now) {
  if (!rt.cloudFetchedAt) return 0xFFFF;
  long d = (long)(now - rt.cloudFetchedAt);
  if (d < 0) d = 0;
  return (uint16_t)(d / 3600);
}

bool tempRange(int8_t& lo, int8_t& hi) {
  bool any = false;
  for (uint8_t i = 0; i < rt.cloud.hours; i++) {
    int8_t t = rt.cloud.temp[i];
    if (t == -128) continue;
    if (!any) { lo = hi = t; any = true; }
    else { if (t < lo) lo = t; if (t > hi) hi = t; }
  }
  return any;
}

uint8_t maxRain() {
  uint8_t m = 0;
  for (uint8_t i = 0; i < rt.cloud.hours; i++)
    if (rt.cloud.rain[i] > m) m = rt.cloud.rain[i];
  return m;
}

float totalMm() {
  uint32_t t10 = 0;
  for (uint8_t i = 0; i < rt.cloud.hours; i++) t10 += rt.cloud.mm10[i];
  return (float)t10 / 10.0f;
}

}  // namespace weather
