// preview.cpp - renders the barograph screens on a PC into PNG/PBM files,
// using the same display.cpp as the firmware. See tools/Makefile.
#include <Adafruit_GFX.h>

#include <cmath>
#include <cstdio>
#include <string>

#include "../src/app_state.h"
#include "../src/display.h"

using namespace baro;

static History hist;
static MinuteSeries minutes;
static AlarmState alarmSt;
static Settings settings = {1, 0, 2, 52, 1, 1, 1, 0};

static void writePbm(GFXcanvas1 &c, const std::string &name) {
  FILE *f = fopen(name.c_str(), "wb");
  fprintf(f, "P4\n%d %d\n", c.width(), c.height());
  const uint8_t *b = c.getBuffer();
  // GFXcanvas1 bit set = "white" (color != 0); PBM bit set = black
  int bytes = (c.width() + 7) / 8 * c.height();
  for (int i = 0; i < bytes; i++) fputc(~b[i] & 0xFF, f);
  fclose(f);
}

// scenario: 0 = quiet, 1 = approaching depression (strong wind), 2 = gale
static AppState build(int scenario, uint32_t now) {
  hist.clear(HISTORY_INTERVAL_S);
  minutes.clear(60);
  alarmSt.clear();
  for (int i = HISTORY_SLOTS - 1; i >= 0; i--) {
    float hAgo = i * HISTORY_INTERVAL_S / 3600.0f;
    float p = 1019.0f + 2.0f * sinf(hAgo / 9.0f) + 0.15f * sinf(i * 1.7f);
    if (scenario >= 1 && hAgo < 12) p -= (12 - hAgo) * 0.9f;
    if (scenario >= 1 && hAgo < 3) p -= (3 - hAgo) * 0.5f;
    if (scenario == 2 && hAgo < 4) p -= (4 - hAgo) * 1.2f;
    if (i == 200 || i == 201) continue;  // a small data gap
    hist.push(now - i * HISTORY_INTERVAL_S, p);
  }
  float last;
  hist.get(0, last);
  float rate = scenario == 2 ? 3.0f : (scenario == 1 ? 1.7f : 0.1f);
  for (int i = MINUTE_SLOTS - 1; i >= 0; i--) minutes.push(now - i * 60, last + i * rate / 60.0f);

  AppState s;
  s.hist = &hist;
  s.minutes = &minutes;
  s.alarm = &alarmSt;
  s.settings = &settings;
  s.nowUtc = now;
  s.timeValid = true;
  s.sensorOk = true;
  s.correction = settings.altitudeM * 0.12f;
  s.havePressure = true;
  s.pressure = last + s.correction;
  s.haveTemp = true;
  s.tempC = 17.4f;
  s.humidity = 78;
  s.metrics.set(M_1H, trendOver(minutes, 3600));
  s.metrics.set(M_3H, trendOver(hist, 3 * 3600));
  s.metrics.set(M_12H, trendOver(hist, 12 * 3600));
  s.metrics.set(M_24H, trendOver(hist, 24 * 3600));
  float j;
  if (pressureJump(minutes, 15, j)) { s.metrics.valid[M_JUMP] = true; s.metrics.value[M_JUMP] = j; }
  s.trend6h = trendOver(hist, 6 * 3600);
  AlarmConfig cfg;
  evaluateAlarms(s.metrics, cfg, alarmSt, now);
  return s;
}

int main(int argc, char **argv) {
  std::string out = argc > 1 ? argv[1] : ".";
  uint32_t now = 1794830400 + 14 * 3600 + 37 * 60;  // a November afternoon
  GFXcanvas1 c(400, 300);
  struct { const char *name; int scen; Screen scr; uint8_t lang; } shots[] = {
      {"main_quiet", 0, SCR_MAIN, 0},   {"main_strong", 1, SCR_MAIN, 0},
      {"main_gale_en", 2, SCR_MAIN, 1}, {"long_72h", 1, SCR_LONG, 0},
      {"trend", 2, SCR_TREND, 0},       {"dial", 1, SCR_DIAL, 0},
      {"dial_quiet_en", 0, SCR_DIAL, 1},       {"settings", 0, SCR_SETTINGS, 0},
  };
  for (auto &sh : shots) {
    settings.lang = sh.lang;
    AppState s = build(sh.scen, now);
    s.screen = sh.scr;
    s.cursor = 2;
    renderScreen(c, s);
    writePbm(c, out + "/" + sh.name + ".pbm");
    printf("%s: level %d, 3h %+.2f\n", sh.name, alarmSt.level, s.metrics.value[M_3H]);
  }
  return 0;
}
