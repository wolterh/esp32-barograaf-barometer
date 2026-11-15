// display.cpp - screen layouts for the 400x300 e-paper.
#include "display.h"

#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>
#include <Fonts/FreeSansBold24pt7b.h>
#include <Fonts/FreeSansBold9pt7b.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "strings.h"

using namespace baro;

static const uint16_t BLACK = 0x0000;
static const uint16_t WHITE = 0xFFFF;
static const int W = 400, H = 300;

// ------------------------------------------------------------ time helpers
// Days-from-civil / civil-from-days (H. Hinnant), valid for all our dates.
static int32_t daysFromCivil(int y, unsigned m, unsigned d) {
  y -= m <= 2;
  const int era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = (unsigned)(y - era * 400);
  const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + (int32_t)doe - 719468;
}

LocalTime toLocal(uint32_t utcEpoch, int tzHours) {
  int64_t t = (int64_t)utcEpoch + (int64_t)tzHours * 3600;
  int32_t days = (int32_t)(t / 86400);
  int32_t secs = (int32_t)(t % 86400);
  LocalTime lt;
  lt.hour = secs / 3600;
  lt.minute = (secs / 60) % 60;
  lt.second = secs % 60;
  lt.wday = (int)((days + 4) % 7);  // 1970-01-01 was a Thursday; 0 = Sunday
  int32_t z = days + 719468;
  const int era = (z >= 0 ? z : z - 146096) / 146097;
  const unsigned doe = (unsigned)(z - era * 146097);
  const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  int y = (int)yoe + era * 400;
  const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const unsigned mp = (5 * doy + 2) / 153;
  lt.day = (int)(doy - (153 * mp + 2) / 5 + 1);
  lt.month = (int)(mp < 10 ? mp + 3 : mp - 9);
  lt.year = y + (lt.month <= 2);
  return lt;
}

uint32_t fromLocal(const LocalTime &t, int tzHours) {
  int64_t days = daysFromCivil(t.year, (unsigned)t.month, (unsigned)t.day);
  int64_t e = days * 86400 + t.hour * 3600 + t.minute * 60 + t.second - (int64_t)tzHours * 3600;
  return e < 0 ? 0 : (uint32_t)e;
}

// ------------------------------------------------------------ text helpers
enum Align { LEFT, CENTER, RIGHT };

static int textWidth(Adafruit_GFX &g, const char *s) {
  int16_t x1, y1;
  uint16_t w, h;
  g.getTextBounds(s, 0, 100, &x1, &y1, &w, &h);
  return (int)w + x1;
}

static void text(Adafruit_GFX &g, int x, int y, const char *s, const GFXfont *f,
                 Align a = LEFT, uint16_t color = BLACK) {
  g.setFont(f);
  g.setTextColor(color);
  int w = textWidth(g, s);
  if (a == CENTER) x -= w / 2;
  if (a == RIGHT) x -= w;
  g.setCursor(x, y);
  g.print(s);
}

static void dottedH(Adafruit_GFX &g, int x0, int x1, int y) {
  for (int x = x0; x <= x1; x += 4) g.drawPixel(x, y, BLACK);
}
static void dottedV(Adafruit_GFX &g, int x, int y0, int y1) {
  for (int y = y0; y <= y1; y += 4) g.drawPixel(x, y, BLACK);
}

static void thickLine(Adafruit_GFX &g, int x0, int y0, int x1, int y1) {
  g.drawLine(x0, y0, x1, y1, BLACK);
  g.drawLine(x0, y0 + 1, x1, y1 + 1, BLACK);
  g.drawLine(x0 + 1, y0, x1 + 1, y1, BLACK);
}

static void formatTendency(char *buf, size_t len, const LangText &L, uint8_t lang,
                           const Trend &t3) {
  TendencyClass c = classify3h(t3);
  if (c == TEND_UNKNOWN) { snprintf(buf, len, "%s", L.noData); return; }
  if (c == TEND_STEADY) { snprintf(buf, len, "%s", L.tendency[c]); return; }
  const char *dir = t3.change > 0 ? L.rising : L.falling;
  const char *adv = L.tendency[c];
  if (!adv[0]) snprintf(buf, len, "%s", dir);
  else if (lang == 1) snprintf(buf, len, "%s %s", dir, adv);
  else snprintf(buf, len, "%s %s", adv, dir);
}

void formatReason(char *buf, size_t len, uint8_t ruleId, const AppState &s) {
  const LangText &L = langText(s.settings->lang);
  if (ruleId >= R_COUNT) { buf[0] = 0; return; }
  const RuleDef &r = RULES[ruleId];
  if (r.id == R_BOMB24H) {
    AlarmConfig cfg;
    cfg.sensitivity = sensitivityFactor(s.settings->sensitivity);
    cfg.latitudeDeg = s.settings->latitude;
    snprintf(buf, len, L.reason[ruleId], ruleThreshold(r, cfg));
    return;
  }
  snprintf(buf, len, L.reason[ruleId], s.metrics.value[r.metric]);
}

// Arrow showing the 3 h tendency: angle grows with the rate of change.
static void trendArrow(Adafruit_GFX &g, int cx, int cy, int len, const Trend &t) {
  if (!t.valid) {
    text(g, cx, cy + 6, "?", &FreeSansBold12pt7b, CENTER);
    return;
  }
  float deg = t.change * 15.0f;  // 6 hPa/3h -> 90 degrees
  if (deg > 80) deg = 80;
  if (deg < -80) deg = -80;
  float a = deg * 3.14159265f / 180.0f;
  float dx = cosf(a), dy = -sinf(a);
  int x0 = cx - (int)(dx * len / 2), y0 = cy - (int)(dy * len / 2);
  int x1 = cx + (int)(dx * len / 2), y1 = cy + (int)(dy * len / 2);
  for (int o = -1; o <= 1; o++) {
    g.drawLine(x0 + o, y0, x1 + o, y1, BLACK);
    g.drawLine(x0, y0 + o, x1, y1 + o, BLACK);
  }
  // head
  float hx = -dx, hy = -dy;  // back along the shaft
  float px = -dy, py = dx;   // perpendicular
  int hl = 10, hw = 7;
  g.fillTriangle(x1 + (int)(dx * 3), y1 + (int)(dy * 3),
                 x1 + (int)(hx * hl + px * hw), y1 + (int)(hy * hl + py * hw),
                 x1 + (int)(hx * hl - px * hw), y1 + (int)(hy * hl - py * hw), BLACK);
}

// ------------------------------------------------------------ graph
static void drawGraph(Adafruit_GFX &g, const AppState &s, int hours, int gx0, int gy0, int gx1,
                      int gy1) {
  const LangText &L = langText(s.settings->lang);
  const History &h = *s.hist;
  const int slots = hours * 3600 / HISTORY_INTERVAL_S;
  // newest point is drawn at its real time relative to "now"
  int32_t lag = h.staleSlots(s.nowUtc);
  int32_t offsetS = (int32_t)(s.nowUtc - h.newestEpoch);
  if (h.count == 0) offsetS = 0;

  // History holds station pressure; the current correction (height +
  // calibration) is applied here so changing it never creates a fake step.
  const float corr = s.correction;
  float lo = 1e9f, hi = -1e9f;
  int n = 0;
  for (int ago = 0; ago < h.count && ago <= slots; ago++) {
    float p;
    if (!h.get(ago, p)) continue;
    p += corr;
    if (p < lo) lo = p;
    if (p > hi) hi = p;
    n++;
  }
  g.drawRect(gx0, gy0, gx1 - gx0 + 1, gy1 - gy0 + 1, BLACK);
  if (n == 0 || lag > slots) {
    text(g, (gx0 + gx1) / 2, (gy0 + gy1) / 2, L.noData, &FreeSansBold12pt7b, CENTER);
    return;
  }
  static const float spans[] = {10, 16, 20, 30, 40, 60, 80, 120};
  float need = (hi - lo) * 1.1f + 1.0f;
  float span = spans[7];
  for (float sp : spans) if (sp >= need) { span = sp; break; }
  float step = span <= 10 ? 2 : (span <= 20 ? 4 : (span <= 40 ? 5 : 10));
  if (span == 16) step = 4;
  float mid = (lo + hi) / 2.0f;
  float yMin = floorf((mid - span / 2) / step) * step;
  if (yMin + span < hi) yMin += step;
  float yMax = yMin + span;

  auto yOf = [&](float p) { return gy1 - (int)lroundf((p - yMin) / (yMax - yMin) * (gy1 - gy0)); };
  float pxPerS = (float)(gx1 - gx0) / (hours * 3600.0f);
  auto xOf = [&](int32_t secondsAgo) { return gx1 - (int)lroundf(secondsAgo * pxPerS); };

  // horizontal grid + labels
  char buf[32];
  int labelEvery = ((gy1 - gy0) * step / span) < 16 ? 2 : 1;
  int k = 0;
  for (float v = yMin; v <= yMax + 0.01f; v += step, k++) {
    int y = yOf(v);
    if (v > yMin && v < yMax) dottedH(g, gx0, gx1, y);
    if (k % labelEvery == 0) {
      snprintf(buf, sizeof buf, "%d", (int)lroundf(v));
      text(g, gx0 - 3, y + 5, buf, &FreeSans9pt7b, RIGHT);
    }
  }

  // vertical grid aligned to local clock
  int tickH = hours <= 24 ? 3 : 12;
  LocalTime ln = toLocal(s.nowUtc, s.settings->tzHours);
  int32_t sinceTick = ((ln.hour % tickH) * 3600 + ln.minute * 60 + ln.second);
  for (int32_t ago = sinceTick; ago <= hours * 3600; ago += tickH * 3600) {
    int x = xOf(ago);
    if (x <= gx0 + 2) break;
    if (x < gx1 - 1) dottedV(g, x, gy0, gy1);
    LocalTime lt = toLocal(s.nowUtc - (uint32_t)ago, s.settings->tzHours);
    if (hours <= 24) snprintf(buf, sizeof buf, "%02d", lt.hour);
    else if (lt.hour == 0) snprintf(buf, sizeof buf, "%d-%d", lt.day, lt.month);
    else snprintf(buf, sizeof buf, "%02d", lt.hour);
    if (x - gx0 > 16 && gx1 - x > 26) text(g, x, gy1 + 16, buf, &FreeSans9pt7b, CENTER);
    if (hours > 24 && lt.hour == 0) {  // emphasise midnight
      g.drawFastVLine(x, gy0, gy1 - gy0, BLACK);
    }
  }
  text(g, gx1, gy1 + 16, L.now, &FreeSans9pt7b, RIGHT);

  // pressure line
  bool havePrev = false;
  int px = 0, py = 0;
  for (int ago = 0; ago < h.count && ago <= slots; ago++) {
    float p;
    if (!h.get(ago, p)) { havePrev = false; continue; }
    p += corr;
    int32_t sAgo = offsetS + ago * (int32_t)HISTORY_INTERVAL_S;
    int x = xOf(sAgo);
    if (x < gx0) break;
    int y = yOf(p);
    if (havePrev) thickLine(g, x, y, px, py);
    else g.fillRect(x, y, 2, 2, BLACK);
    px = x; py = y; havePrev = true;
  }
  // current value dot at the right edge
  if (s.havePressure && s.pressure >= yMin && s.pressure <= yMax) {
    g.fillCircle(gx1 - 3, yOf(s.pressure), 3, BLACK);
  }
}

// ------------------------------------------------------------ header/footer
static int drawHeader(Adafruit_GFX &g, const AppState &s) {
  const LangText &L = langText(s.settings->lang);
  char buf[64];
  if (s.havePressure) snprintf(buf, sizeof buf, "%.1f", s.pressure);
  else snprintf(buf, sizeof buf, "----.-");
  text(g, 2, 40, buf, &FreeSansBold24pt7b);
  text(g, 4 + textWidth(g, buf) + 2, 40, "hPa", &FreeSans9pt7b);

  trendArrow(g, 248, 24, 34, s.metrics.valid[M_3H] ? Trend{true, s.metrics.value[M_3H]} : Trend{});

  LocalTime lt = toLocal(s.nowUtc, s.settings->tzHours);
  if (s.timeValid) {
    snprintf(buf, sizeof buf, "%02d:%02d", lt.hour, lt.minute);
    text(g, W - 3, 22, buf, &FreeSansBold12pt7b, RIGHT);
    snprintf(buf, sizeof buf, "%02d-%02d-%04d", lt.day, lt.month, lt.year);
    text(g, W - 3, 40, buf, &FreeSans9pt7b, RIGHT);
  } else {
    text(g, W - 3, 22, L.timeUnknown, &FreeSansBold12pt7b, RIGHT);
  }

  // tendency line
  Trend t3 = s.metrics.valid[M_3H] ? Trend{true, s.metrics.value[M_3H]} : Trend{};
  char tend[40];
  formatTendency(tend, sizeof tend, L, s.settings->lang, t3);
  if (t3.valid) snprintf(buf, sizeof buf, "3%s %+.1f  %s", L.hoursSuffix, t3.change, tend);
  else snprintf(buf, sizeof buf, "3%s %s", L.hoursSuffix, tend);
  text(g, 3, 60, buf, &FreeSansBold9pt7b);
  if (s.metrics.valid[M_1H]) {
    snprintf(buf, sizeof buf, "1%s %+.1f", L.hoursSuffix, s.metrics.value[M_1H]);
    text(g, W - 3, 60, buf, &FreeSans9pt7b, RIGHT);
  }

  int y = 66;
  uint8_t lvl = s.alarm->level;
  if (s.testAlarm || lvl > LVL_NONE) {
    g.fillRect(0, y, W, 42, BLACK);
    if (s.testAlarm) {
      text(g, 6, y + 18, L.testAlarm, &FreeSansBold12pt7b, LEFT, WHITE);
      text(g, 6, y + 37, L.ackHint, &FreeSans9pt7b, LEFT, WHITE);
    } else {
      bool unack = alarmUnacknowledged(*s.alarm);
      snprintf(buf, sizeof buf, "%s%s", unack ? "! " : "", L.level[lvl]);
      text(g, 6, y + 18, buf, &FreeSansBold12pt7b, LEFT, WHITE);
      formatReason(buf, sizeof buf, s.alarm->primary, s);
      text(g, 6, y + 37, buf, &FreeSans9pt7b, LEFT, WHITE);
    }
    y += 46;
  } else {
    g.drawFastHLine(0, y, W, BLACK);
    y += 6;
  }
  return y;
}

static void drawFooter(Adafruit_GFX &g, const AppState &s) {
  const LangText &L = langText(s.settings->lang);
  char buf[64];
  if (!s.sensorOk) {
    text(g, 3, H - 4, L.noSensor, &FreeSansBold9pt7b);
  } else if (s.alarm->level > LVL_NONE) {
    text(g, 3, H - 4, L.windHint[s.alarm->level], &FreeSansBold9pt7b);
  } else if (s.haveTemp) {
    if (s.humidity >= 0) snprintf(buf, sizeof buf, "%.1f C  %.0f%%", s.tempC, s.humidity);
    else snprintf(buf, sizeof buf, "%.1f C", s.tempC);
    text(g, 3, H - 4, buf, &FreeSans9pt7b);
  }
  if (alarmUnacknowledged(*s.alarm)) {
    text(g, W - 3, H - 4, L.ackHint, &FreeSans9pt7b, RIGHT);
  } else if (s.timeValid) {
    LocalTime lt = toLocal(s.nowUtc, s.settings->tzHours);
    snprintf(buf, sizeof buf, "%s %02d:%02d", L.updated, lt.hour, lt.minute);
    text(g, W - 3, H - 4, buf, &FreeSans9pt7b, RIGHT);
  }
}

// ------------------------------------------------------------ screens
static void screenMain(Adafruit_GFX &g, const AppState &s, int hours) {
  int top = drawHeader(g, s);
  drawGraph(g, s, hours, 42, top + 4, W - 6, H - 40);
  drawFooter(g, s);
}

static void trendRow(Adafruit_GFX &g, const LangText &L, uint8_t lang, int y, int hours,
                     bool valid, float change) {
  char buf[48];
  snprintf(buf, sizeof buf, "%2d %s", hours, L.hoursSuffix);
  text(g, 8, y, buf, &FreeSansBold9pt7b);
  if (!valid) { text(g, 90, y, L.noData, &FreeSans9pt7b); return; }
  snprintf(buf, sizeof buf, "%+.1f hPa", change);
  text(g, 160, y, buf, &FreeSans9pt7b, RIGHT);
  // classify as an equivalent 3 h rate
  Trend eq{true, change * 3.0f / hours};
  formatTendency(buf, sizeof buf, L, lang, eq);
  text(g, 180, y, buf, &FreeSans9pt7b);
}

static void screenTrend(Adafruit_GFX &g, const AppState &s) {
  const LangText &L = langText(s.settings->lang);
  uint8_t lang = s.settings->lang;
  char buf[64];
  text(g, 4, 22, L.screenTrend, &FreeSansBold12pt7b);
  if (s.havePressure) {
    snprintf(buf, sizeof buf, "%.1f hPa", s.pressure);
    text(g, W - 4, 22, buf, &FreeSansBold12pt7b, RIGHT);
  }
  g.drawFastHLine(0, 30, W, BLACK);
  int y = 48;
  trendRow(g, L, lang, y, 1, s.metrics.valid[M_1H], s.metrics.value[M_1H]); y += 18;
  trendRow(g, L, lang, y, 3, s.metrics.valid[M_3H], s.metrics.value[M_3H]); y += 18;
  trendRow(g, L, lang, y, 6, s.trend6h.valid, s.trend6h.change); y += 18;
  trendRow(g, L, lang, y, 12, s.metrics.valid[M_12H], s.metrics.value[M_12H]); y += 18;
  trendRow(g, L, lang, y, 24, s.metrics.valid[M_24H], s.metrics.value[M_24H]); y += 18;
  if (s.metrics.valid[M_JUMP]) {
    snprintf(buf, sizeof buf, "15 min: %+.1f hPa", s.metrics.value[M_JUMP]);
    text(g, 8, y, buf, &FreeSans9pt7b);
  }
  y += 7;
  g.drawFastHLine(0, y, W, BLACK);
  y += 18;
  text(g, 4, y, L.activeRules, &FreeSansBold9pt7b);
  y += 18;
  int shown = 0;
  for (int i = 0; i < R_COUNT && shown < 3; i++) {
    if (!((s.alarm->activeMask >> i) & 1u)) continue;
    formatReason(buf, sizeof buf, (uint8_t)i, s);
    char line[80];
    snprintf(line, sizeof line, "%c %s", RULES[i].level >= LVL_GALE ? '#' : '*', buf);
    text(g, 8, y, line, &FreeSans9pt7b);
    y += 18;
    shown++;
  }
  if (!shown) { text(g, 8, y, L.noActiveRules, &FreeSans9pt7b); y += 18; }
  y = 248;
  g.drawFastHLine(0, y - 16, W, BLACK);
  for (int i = 0; i < 3; i++) text(g, 4, y + i * 17, L.rulesOfThumb[i], &FreeSans9pt7b);
}

static void settingValue(char *buf, size_t len, uint8_t item, const AppState &s) {
  const Settings &st = *s.settings;
  const LangText &L = langText(st.lang);
  LocalTime lt = toLocal(s.nowUtc, st.tzHours);
  switch (item) {
    case SET_SENSITIVITY: snprintf(buf, len, "%s", L.sens[st.sensitivity % 3]); break;
    case SET_OFFSET: snprintf(buf, len, "%+.1f", st.offsetDeci / 10.0f); break;
    case SET_ALTITUDE: snprintf(buf, len, "%d", st.altitudeM); break;
    case SET_LATITUDE: snprintf(buf, len, "%d", st.latitude); break;
    case SET_TZ: snprintf(buf, len, "%+d", st.tzHours); break;
    case SET_BUZZER: snprintf(buf, len, "%s", L.onOff[st.buzzer ? 1 : 0]); break;
    case SET_SLEEP: snprintf(buf, len, "%s", L.onOff[st.sleep ? 1 : 0]); break;
    case SET_LANG: snprintf(buf, len, "%s", L.langName[st.lang ? 1 : 0]); break;
    case SET_YEAR: snprintf(buf, len, "%04d", lt.year); break;
    case SET_MONTH: snprintf(buf, len, "%02d", lt.month); break;
    case SET_DAY: snprintf(buf, len, "%02d", lt.day); break;
    case SET_HOUR: snprintf(buf, len, "%02d", lt.hour); break;
    case SET_MINUTE: snprintf(buf, len, "%02d", lt.minute); break;
    default: snprintf(buf, len, "%s", L.pressOk); break;
  }
}

static void screenSettings(Adafruit_GFX &g, const AppState &s) {
  const LangText &L = langText(s.settings->lang);
  text(g, 4, 20, L.screenSettings, &FreeSansBold12pt7b);
  g.drawFastHLine(0, 26, W, BLACK);
  const int rowH = 18;
  int y0 = 30;
  char val[32];
  for (int i = 0; i < SET_COUNT; i++) {
    int y = y0 + i * rowH;
    bool sel = (i == s.cursor);
    uint16_t fg = sel ? WHITE : BLACK;
    if (sel) g.fillRect(0, y, W, rowH, BLACK);
    text(g, 6, y + 14, L.setting[i], sel ? &FreeSansBold9pt7b : &FreeSans9pt7b, LEFT, fg);
    settingValue(val, sizeof val, (uint8_t)i, s);
    char shown[40];
    if (sel && s.editing) snprintf(shown, sizeof shown, "< %s >", val);
    else snprintf(shown, sizeof shown, "%s", val);
    text(g, W - 6, y + 14, shown, sel ? &FreeSansBold9pt7b : &FreeSans9pt7b, RIGHT, fg);
  }
}

// ------------------------------------------------------------ classic dial
// Aneroid-style barometer: 950..1050 hPa over a 270 degree arc, the weather
// words at the traditional positions (28.5/29/29.5/30/30.5 inHg) and a
// reference ("set") hand showing the pressure of 3 hours ago, so the
// tendency is read like on a brass ship's barometer: needle vs. set hand.
static const float DIAL_MIN = 950.0f, DIAL_MAX = 1050.0f;

static float dialAngle(float p) {  // radians, math orientation
  if (p < DIAL_MIN - 2) p = DIAL_MIN - 2;
  if (p > DIAL_MAX + 2) p = DIAL_MAX + 2;
  float deg = 225.0f - (p - DIAL_MIN) / (DIAL_MAX - DIAL_MIN) * 270.0f;
  return deg * 3.14159265f / 180.0f;
}

static void polar(int cx, int cy, float r, float a, int &x, int &y) {
  x = cx + (int)lroundf(r * cosf(a));
  y = cy - (int)lroundf(r * sinf(a));
}

static void screenDial(Adafruit_GFX &g, const AppState &s) {
  const LangText &L = langText(s.settings->lang);
  const int cx = 200, cy = 152, R = 146;
  char buf[48];

  // bezel
  g.drawCircle(cx, cy, R, BLACK);
  g.drawCircle(cx, cy, R - 1, BLACK);
  g.drawCircle(cx, cy, R - 4, BLACK);

  // scale: minor tick every hPa, medium every 5, major + number every 10
  for (int p = (int)DIAL_MIN; p <= (int)DIAL_MAX; p++) {
    float a = dialAngle((float)p);
    int len = (p % 10 == 0) ? 14 : (p % 5 == 0 ? 9 : 5);
    int x0, y0, x1, y1;
    polar(cx, cy, R - 6, a, x0, y0);
    polar(cx, cy, R - 6 - len, a, x1, y1);
    g.drawLine(x0, y0, x1, y1, BLACK);
    if (p % 10 == 0) {
      int x2, y2;
      polar(cx, cy, R - 6 - len + 1, a + 0.006f, x2, y2);  // thicken
      g.drawLine(x0, y0, x2, y2, BLACK);
      polar(cx, cy, R - 36, a, x2, y2);
      snprintf(buf, sizeof buf, "%d", p);
      text(g, x2, y2 + 5, buf, &FreeSans9pt7b, CENTER);
    }
  }
  // inner scale ring
  for (int i = 0; i <= 270; i += 2) {
    float a = (225.0f - i) * 3.14159265f / 180.0f;
    int x, y;
    polar(cx, cy, R - 21, a, x, y);
    g.drawPixel(x, y, BLACK);
  }

  // weather words at the classic positions
  // (965, 982, 999, 1016, 1033 hPa); placed by hand so that the horizontal
  // text stays clear of the numbers and the hub.
  static const int WORD_DX[5] = {-70, -66, 0, 66, 68};
  static const int WORD_DY[5] = {15, -36, -66, -36, 15};
  for (int i = 0; i < 5; i++)
    text(g, cx + WORD_DX[i], cy + WORD_DY[i], L.dialWords[i], &FreeSans9pt7b, CENTER);

  // reference (set) hand: pressure 3 h ago, thin with an open ring
  if (s.havePressure && s.metrics.valid[M_3H]) {
    float a = dialAngle(s.pressure - s.metrics.value[M_3H]);
    int x, y, xr, yr;
    polar(cx, cy, R - 25, a, x, y);
    polar(cx, cy, R - 29, a, xr, yr);
    g.drawLine(cx, cy, xr, yr, BLACK);
    g.fillCircle(x, y, 4, WHITE);
    g.drawCircle(x, y, 4, BLACK);
    g.drawCircle(x, y, 3, BLACK);
  }

  // main needle: tapered, with a counterweight tail
  if (s.havePressure) {
    float a = dialAngle(s.pressure);
    float pa = a + 3.14159265f / 2;  // perpendicular
    int tx, ty, b1x, b1y, b2x, b2y, ex, ey;
    polar(cx, cy, R - 10, a, tx, ty);
    polar(cx, cy, 6, pa, b1x, b1y);
    polar(cx, cy, -6, pa, b2x, b2y);
    g.fillTriangle(tx, ty, b1x, b1y, b2x, b2y, BLACK);
    polar(cx, cy, -24, a, ex, ey);
    g.fillTriangle(ex, ey, b1x, b1y, b2x, b2y, BLACK);
    g.fillCircle(ex, ey, 5, BLACK);
  }
  g.fillCircle(cx, cy, 9, BLACK);
  g.fillCircle(cx, cy, 3, WHITE);

  // digital readout in the gap at the bottom of the dial
  if (s.havePressure) snprintf(buf, sizeof buf, "%.1f hPa", s.pressure);
  else snprintf(buf, sizeof buf, "---- hPa");
  text(g, cx, cy + 100, buf, &FreeSansBold12pt7b, CENTER);
  if (s.metrics.valid[M_3H]) {
    snprintf(buf, sizeof buf, "3%s %+.1f", L.hoursSuffix, s.metrics.value[M_3H]);
    text(g, cx, cy + 120, buf, &FreeSans9pt7b, CENTER);
  }

  // corners: time, alarm state, legend
  LocalTime lt = toLocal(s.nowUtc, s.settings->tzHours);
  if (s.timeValid) snprintf(buf, sizeof buf, "%02d:%02d", lt.hour, lt.minute);
  else snprintf(buf, sizeof buf, "%s", L.timeUnknown);
  text(g, W - 3, 20, buf, &FreeSansBold12pt7b, RIGHT);
  if (s.metrics.valid[M_3H]) text(g, W - 3, H - 4, L.dialRefHint, &FreeSans9pt7b, RIGHT);
  uint8_t lvl = s.alarm->level;
  if (s.testAlarm || lvl > LVL_NONE) {
    const char *t = s.testAlarm ? L.testAlarm : L.windHint[lvl];
    g.setFont(&FreeSansBold9pt7b);
    int w = textWidth(g, t);
    g.fillRect(0, H - 22, w + 10, 22, BLACK);
    text(g, 5, H - 6, t, &FreeSansBold9pt7b, LEFT, WHITE);
  }
  if (!s.sensorOk) text(g, 3, 20, L.noSensor, &FreeSansBold9pt7b);
}

void renderScreen(Adafruit_GFX &g, const AppState &s) {
  g.fillScreen(WHITE);
  switch (s.screen) {
    case SCR_LONG: {
      screenMain(g, s, 72);
      // label the view inside the graph corner
      const LangText &L = langText(s.settings->lang);
      int top = (s.testAlarm || s.alarm->level) ? 112 : 72;
      text(g, 48, top + 20, L.screenLong, &FreeSans9pt7b);
      break;
    }
    case SCR_TREND: screenTrend(g, s); break;
    case SCR_DIAL: screenDial(g, s); break;
    case SCR_SETTINGS: screenSettings(g, s); break;
    default: screenMain(g, s, 24); break;
  }
}
