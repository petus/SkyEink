// gp.cpp

#include <Arduino.h>
#include <string.h>
#include <math.h>
#include "gp.h"
#include "hal.h"
#include "config.h"
#include "i18n.h"
#include "runtime.h"
#include "version.h"
#include "astro.h"

namespace gp {

namespace {

// The version is part of the file name. When the Rec struct changes, an old
// cache is simply ignored instead of being misread byte by byte.
const char* CACHE = "/" FW_SLUG "/gp2.dat";
const char* TMP   = "/" FW_SLUG "/gp2.tmp";
const char* META  = "/" FW_SLUG "/gp2.meta";

// Written next to the cache once a download completes.
struct Meta {
  uint32_t magic;
  int32_t  fetchedAt;
  int32_t  oldest;
  uint16_t count;
  uint16_t pad;
};
const uint32_t META_MAGIC = 0x534B4D31UL;   // "SKM1"

hal::StoreFile outFile = nullptr;
hal::StoreFile inFile  = nullptr;
uint16_t       nRecs   = 0;
time_t         oldest  = 0;
// Once a single write fails there is no point carrying on. The rest of the
// file would be useless anyway, and promoting a truncated cache is worse than
// keeping the previous one.
bool           writeFailed = false;

// CSV columns are located by name, not by position.
enum ColIdx {
  C_NAME = 0, C_INTDES, C_EPOCH, C_MM, C_ECC, C_INC, C_RAAN,
  C_ARGP, C_MA, C_NORAD, C_BSTAR, C_MMDOT, C_MMDDOT, C_N
};
int8_t colMap[C_N];

const char* COLNAME[C_N] = {
  "OBJECT_NAME", "OBJECT_ID", "EPOCH", "MEAN_MOTION", "ECCENTRICITY",
  "INCLINATION", "RA_OF_ASC_NODE", "ARG_OF_PERICENTER", "MEAN_ANOMALY",
  "NORAD_CAT_ID", "BSTAR", "MEAN_MOTION_DOT", "MEAN_MOTION_DDOT"
};

void resetColMap() { for (int i = 0; i < C_N; i++) colMap[i] = -1; }

// Splits a line into fields. Returns how many were found.
uint8_t split(char* line, char** f, uint8_t maxF) {
  uint8_t n = 0;
  char* p = line;
  while (n < maxF) {
    f[n++] = p;
    char* c = strchr(p, ',');
    if (!c) break;
    *c = 0;
    p = c + 1;
  }
  return n;
}

// False when the header does not look like the expected CSV. That happens
// when the server returns an error page with status 200, or when something
// unrelated ends up in the body.
bool parseHeader(char* line) {
  resetColMap();
  char* f[32];
  uint8_t n = split(line, f, 32);
  for (uint8_t i = 0; i < n; i++) {
    for (uint8_t c = 0; c < C_N; c++) {
      if (colMap[c] < 0 && strcmp(f[i], COLNAME[c]) == 0) { colMap[c] = (int8_t)i; break; }
    }
  }
  // Without these three columns nothing can be built from the response.
  bool ok = colMap[C_EPOCH] >= 0 && colMap[C_MM] >= 0 && colMap[C_INC] >= 0;
  if (!ok) {
    line[60] = 0;   // log only the beginning so it does not flood
    Serial.printf("[gp] neocekavana hlavicka: %s\n", line);
  }
  return ok;
}

// ISO epoch 2026-08-30T12:34:56.789012 to YYDDD.DDDDDDDD and to Unix time.
bool epochConvert(const char* iso, char* out14, time_t& unixOut) {
  int Y, Mo, D, H, Mi;
  double S = 0.0;
  if (sscanf(iso, "%d-%d-%dT%d:%d:%lf", &Y, &Mo, &D, &H, &Mi, &S) < 5) return false;

  struct tm t = {};
  t.tm_year = Y - 1900; t.tm_mon = Mo - 1; t.tm_mday = D;
  t.tm_hour = H; t.tm_min = Mi; t.tm_sec = (int)S;
  time_t e = astro::tmToUtc(t);
  unixOut = e;

  struct tm jan = {};
  jan.tm_year = Y - 1900; jan.tm_mon = 0; jan.tm_mday = 1;
  time_t y0 = astro::tmToUtc(jan);

  double doy = (double)(e - y0) / 86400.0 + 1.0
             + (S - (int)S) / 86400.0;
  snprintf(out14, 15, "%02d%012.8f", Y % 100, doy);
  return true;
}

// A number in TLE exponent notation: 0.000012345 -> " 12345-4".
void expField(double v, char* out9) {
  if (v == 0.0) { strcpy(out9, " 00000-0"); return; }
  char sign = v < 0 ? '-' : ' ';
  double a = fabs(v);
  int e = 0;
  while (a >= 1.0)  { a /= 10.0; e++; }
  while (a <  0.1)  { a *= 10.0; e--; }
  int mant = (int)(a * 100000.0 + 0.5);
  if (mant > 99999) { mant /= 10; e++; }
  snprintf(out9, 9, "%c%05d%c%d", sign, mant, e < 0 ? '-' : '+', abs(e));
}

char checksum(const char* line) {
  int s = 0;
  for (int i = 0; i < 68 && line[i]; i++) {
    if (line[i] >= '0' && line[i] <= '9') s += line[i] - '0';
    else if (line[i] == '-') s += 1;
  }
  return (char)('0' + (s % 10));
}

// Assembles both lines. The catalogue number is a placeholder because SGP4
// ignores it; the real NORAD ID is kept separately.
void buildTle(const char* epoch14, const char* intdes,
              double mmdot, double mmddot, double bstar,
              double inc, double raan, double ecc,
              double argp, double ma, double mm,
              char* l1, char* l2) {
  char des[9] = "00000A  ";
  if (intdes && strlen(intdes) >= 8) {
    // 2026-025A -> 26025A
    snprintf(des, sizeof(des), "%c%c%c%c%c%-3s",
             intdes[2], intdes[3], intdes[5], intdes[6], intdes[7],
             intdes + 8);
  }

  char n1[9], n2[9];
  expField(mmddot, n1);
  expField(bstar,  n2);

  char ndot[11];
  snprintf(ndot, sizeof(ndot), "%c.%08d",
           mmdot < 0 ? '-' : ' ', (int)(fabs(mmdot) * 1e8 + 0.5) % 100000000);

  int eccInt = (int)(ecc * 1e7 + 0.5);
  if (eccInt > 9999999) eccInt = 9999999;

  snprintf(l1, 70, "1 99999U %-8s %-14s %-10s %-8s %-8s 0  9990",
           des, epoch14, ndot, n1, n2);
  l1[68] = checksum(l1); l1[69] = 0;

  snprintf(l2, 70, "2 99999 %8.4f %8.4f %07d %8.4f %8.4f %11.8f    10",
           inc, raan, eccInt, argp, ma, mm);
  l2[68] = checksum(l2); l2[69] = 0;
}

// The highest latitude the satellite can ever reach.
double maxLatitude(double incDeg) {
  double i = incDeg > 90.0 ? 180.0 - incDeg : incDeg;
  return i;
}

bool handleRow(char* line, bool starlinkGroup) {
  char* f[32];
  uint8_t n = split(line, f, 32);
  if (n < 10) return false;
  for (int c = 0; c < C_N; c++)
    if (colMap[c] >= 0 && colMap[c] >= n) return false;

  auto col = [&](ColIdx c) -> const char* {
    return colMap[c] >= 0 ? f[colMap[c]] : "";
  };

  double mm  = atof(col(C_MM));
  double inc = atof(col(C_INC));
  if (mm <= 0.0) return false;

  // An object that never reaches our latitude is not worth propagating. The
  // 15 degree margin covers the radius of the visibility circle.
  if (maxLatitude(inc) + 15.0 < fabs((double)cfg.lat)) return false;

  // Starlink is filtered to orbits below roughly 500 km, i.e. fresh trains.
  if (starlinkGroup && mm < cfg.slMinMeanMotion) return false;

  char epoch14[16];
  time_t epochUnix;
  if (!epochConvert(col(C_EPOCH), epoch14, epochUnix)) return false;

  Rec r;
  memset(&r, 0, sizeof(r));
  // Celestrak lists composite bodies more than once, for example ISS (ZARYA)
  // and ISS (NAUKA). Physically that is one station and the passes come out
  // identical, so the bracket is dropped and duplicates are merged later in
  // passes.cpp.
  {
    const char* src = col(C_NAME);
    size_t k = 0;
    while (src[k] && k < sizeof(r.name) - 1) {
      if (src[k] == '(' ) break;
      r.name[k] = src[k];
      k++;
    }
    while (k > 0 && r.name[k - 1] == ' ') k--;   // trim the space before it
    r.name[k] = 0;
  }
  r.norad  = (uint32_t)atol(col(C_NORAD));
  r.stdMag = guessStdMag(r.name);
  r.inc    = (float)inc;
  r.raan   = (float)atof(col(C_RAAN));
  r.ecc    = (float)atof(col(C_ECC));
  r.mm     = (float)mm;
  r.epoch  = (int32_t)epochUnix;

  buildTle(epoch14, col(C_INTDES),
           atof(col(C_MMDOT)), atof(col(C_MMDDOT)), atof(col(C_BSTAR)),
           inc, atof(col(C_RAAN)), atof(col(C_ECC)),
           atof(col(C_ARGP)), atof(col(C_MA)), mm,
           r.l1, r.l2);

  if (hal::storeWrite(outFile, &r, sizeof(r)) != sizeof(r)) {
    writeFailed = true;
    return false;
  }
  if (!oldest || epochUnix < oldest) oldest = epochUnix;
  nRecs++;
  return true;
}

// State of the streaming parser between HTTP chunks.
struct StreamCtx {
  char    buf[512];
  uint16_t len;
  bool    headerDone;
  bool    starlink;
  bool    full;
  bool    tooLong;      // the line did not fit in the buffer
  uint16_t droppedLong; // how many were dropped because of that
};

bool onChunk(const uint8_t* data, size_t len, void* c) {
  StreamCtx* s = (StreamCtx*)c;
  for (size_t i = 0; i < len; i++) {
    char ch = (char)data[i];
    if (ch == '\r') continue;
    if (ch != '\n') {
      if (s->len < sizeof(s->buf) - 1) s->buf[s->len++] = ch;
      else                             s->tooLong = true;
      continue;
    }
    s->buf[s->len] = 0;
    // A truncated line is discarded. It used to be processed as valid and
    // produced a record with corrupted elements, which showed up nowhere.
    if (s->tooLong) {
      s->droppedLong++;
      s->tooLong = false;
    } else if (s->len) {
      if (!s->headerDone) {
        s->headerDone = true;
        if (!parseHeader(s->buf)) return false;   // no point going on
      } else {
        handleRow(s->buf, s->starlink);
      }
    }
    s->len = 0;
    if (writeFailed) return false;
    if (nRecs >= cfg.maxObjects) { s->full = true; return false; }
  }
  return true;
}

int fetchGroup(const char* group, bool starlink, ProgressFn cb, uint8_t pct) {
  char url[128];
  snprintf(url, sizeof(url),
           "https://celestrak.org/NORAD/elements/gp.php?GROUP=%s&FORMAT=csv",
           group);

  char stage[64];
  snprintf(stage, sizeof(stage), T(S_P_DL), group);
  if (cb) cb(stage, pct);

  StreamCtx s;
  memset(&s, 0, sizeof(s));
  s.starlink = starlink;

  int rc = hal::netGet(url, onChunk, &s, 30000);
  if (s.droppedLong)
    Serial.printf("[gp] %s: %u prilis dlouhych radku zahozeno\n",
                  group, (unsigned)s.droppedLong);
  if (writeFailed) return GP_ERR_STORE;
  if (rc == 403) return GP_ERR_403;   // data unchanged since last time
  if (rc != 200) return GP_ERR_NET;
  return 0;
}

}  // namespace

float guessStdMag(const char* name) {
  // The table lives in the settings, so it can be tuned from the web page
  // without rebuilding. First match wins, which makes the order a priority.
  for (uint8_t i = 0; i < SKY_MAG_RULES; i++) {
    const char* m = cfg.magRules[i].match;
    if (m[0] && strstr(name, m)) return cfg.magRules[i].mag;
  }
  return cfg.magDefault;
}


uint32_t cacheBytesNeeded() {
  return (uint32_t)cfg.maxObjects * sizeof(Rec);
}

int refresh(ProgressFn cb) {
  close();
  nRecs = 0;
  oldest = 0;
  writeFailed = false;

  if (!hal::storeBegin()) return GP_ERR_STORE;

  // Left over from an older cache version. The record format changed, so the
  // file is never read and only takes up space.
  hal::storeRemove("/" FW_SLUG "/gp.dat");

  // The old and the new cache coexist until the rename, so there has to be
  // room for both.
  int32_t freeKb = hal::storeFreeKb();
  uint32_t needKb = (cacheBytesNeeded() * 2) / 1024 + 32;
  if (freeKb >= 0 && (uint32_t)freeKb < needKb) {
    Serial.printf("[gp] malo mista: volno %ld kB, potreba %lu kB\n",
                  (long)freeKb, (unsigned long)needKb);
    return GP_ERR_STORE;
  }

  outFile = hal::storeOpenWrite(TMP);
  if (!outFile) return GP_ERR_STORE;

  struct { const char* g; uint8_t mask; bool sl; } GROUPS[] = {
    {"stations",     GRP_STATIONS, false},
    {"visual",      GRP_VISUAL,   false},
    {"last-30-days",GRP_LAST30,   false},
    {"starlink",    GRP_STARLINK, true },
  };

  bool any = false;
  bool blocked = false;
  uint8_t step = 0, steps = 0;
  for (auto& g : GROUPS) if (cfg.groups & g.mask) steps++;
  if (!steps) steps = 1;

  for (auto& g : GROUPS) {
    if (!(cfg.groups & g.mask)) continue;
    step++;
    int rc = fetchGroup(g.g, g.sl, cb, (uint8_t)(step * 90 / steps));
    if (rc == 0) any = true;
    else if (rc == GP_ERR_403) blocked = true;
    else if (rc == GP_ERR_STORE) break;
    if (writeFailed || nRecs >= cfg.maxObjects) break;
  }

  hal::storeClose(outFile);
  outFile = nullptr;

  // A truncated cache is worse than none because it looks valid. Discard it
  // and keep the previous one.
  if (writeFailed) {
    hal::storeRemove(TMP);
    return GP_ERR_STORE;
  }

  if (!any || nRecs == 0) {
    hal::storeRemove(TMP);
    return blocked ? GP_ERR_403 : GP_ERR_NET;
  }

  // The new cache is only put in place once it is complete, so an
  // interrupted download cannot destroy what is already on the card.
  hal::storeRemove(CACHE);
  if (!hal::storeRename(TMP, CACHE)) { hal::storeRemove(TMP); return GP_ERR_STORE; }

  // The companion record. Without it a cold start cannot tell how old the
  // cache on the card is and would download it all over again.
  {
    Meta m;
    m.magic     = META_MAGIC;
    // hal::timeNow() reads the RTC, which is the authoritative clock here.
    // time(nullptr) would have returned 1970 whenever the run took its time
    // from the RTC without syncing, and the record would then claim the cache
    // was decades old on the next cold start.
    m.fetchedAt = (int32_t)hal::timeNow();
    m.oldest    = (int32_t)oldest;
    m.count     = nRecs;
    m.pad       = 0;
    hal::StoreFile f = hal::storeOpenWrite(META);
    if (f) {
      hal::storeWrite(f, &m, sizeof(m));
      hal::storeClose(f);
    }
  }

  if (cb) cb(T(S_P_DONE), 100);
  return (int)nRecs;
}

bool open() {
  close();
  if (!hal::storeExists(CACHE)) return false;
  size_t sz = hal::storeSize(CACHE);

  // The size has to be a multiple of the record. If it is not, the file is
  // corrupt or from another version, and dropping it beats reading it.
  if (sz == 0 || (sz % sizeof(Rec)) != 0) {
    Serial.printf("[gp] cache ma nesmyslnou velikost %u B, mazu\n",
                  (unsigned)sz);
    hal::storeRemove(CACHE);
    return false;
  }
  nRecs = (uint16_t)(sz / sizeof(Rec));
  if (!nRecs) return false;
  inFile = hal::storeOpenRead(CACHE);
  return inFile != nullptr;
}

void close() {
  if (inFile)  { hal::storeClose(inFile);  inFile = nullptr; }
  if (outFile) { hal::storeClose(outFile); outFile = nullptr; }
}

uint16_t count() { return nRecs; }

bool get(uint16_t index, Rec& out) {
  if (!inFile || index >= nRecs) return false;
  if (!hal::storeSeek(inFile, (uint32_t)index * sizeof(Rec))) return false;
  return hal::storeRead(inFile, &out, sizeof(out)) == sizeof(out);
}

time_t oldestEpoch() { return oldest ? oldest : rt.gpEpochOldest; }

bool cacheInfo(time_t& fetchedAt, uint16_t& count, time_t& oldestOut) {
  if (!hal::storeExists(CACHE) || !hal::storeExists(META)) return false;

  Meta m;
  hal::StoreFile f = hal::storeOpenRead(META);
  if (!f) return false;
  size_t n = hal::storeRead(f, &m, sizeof(m));
  hal::storeClose(f);

  if (n != sizeof(m) || m.magic != META_MAGIC || m.count == 0) return false;

  // The record has to agree with the file it describes, otherwise one of the
  // two is left over from an interrupted write.
  size_t sz = hal::storeSize(CACHE);
  if (sz / sizeof(Rec) != m.count) return false;

  fetchedAt = (time_t)m.fetchedAt;
  count     = m.count;
  oldestOut = (time_t)m.oldest;
  return true;
}

}  // namespace gp
