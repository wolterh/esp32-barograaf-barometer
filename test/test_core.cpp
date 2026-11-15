// Native unit tests for BaroCore (no hardware needed).
//   make -C test        (or: g++ -std=c++17 -I../lib/BaroCore/src test_core.cpp)
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "baro_alarm.h"
#include "baro_series.h"
#include "baro_trend.h"

using namespace baro;

static int failures = 0;
#define CHECK(cond)                                                     \
  do {                                                                  \
    if (!(cond)) {                                                      \
      std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);       \
      failures++;                                                       \
    }                                                                   \
  } while (0)
#define NEAR(a, b, tol) CHECK(std::fabs((a) - (b)) <= (tol))

static const uint32_t T0 = 1789000000;  // some epoch, multiple of 600 not needed

static void testEncode() {
  NEAR(decodeP(encodeP(1013.25f)), 1013.25f, 0.006f);
  NEAR(decodeP(encodeP(950.0f)), 950.0f, 0.006f);
  CHECK(encodeP(700.0f) != NO_DATA);  // clamps, never collides with gap marker
}

static void testSeriesGaps() {
  Series<10> s;
  s.clear(600);
  s.push(T0, 1010.0f);
  s.push(T0 + 600, 1011.0f);
  s.push(T0 + 3 * 600, 1013.0f);  // one slot missing
  float p;
  CHECK(s.count == 4);
  CHECK(s.get(0, p) && std::fabs(p - 1013.0f) < 0.01f);
  CHECK(!s.get(1, p));  // gap
  CHECK(s.get(2, p) && std::fabs(p - 1011.0f) < 0.01f);
  CHECK(s.get(3, p) && std::fabs(p - 1010.0f) < 0.01f);
  // same slot overwrites
  s.push(T0 + 3 * 600 + 10, 1014.0f);
  CHECK(s.count == 4);
  CHECK(s.get(0, p) && std::fabs(p - 1014.0f) < 0.01f);
  // wrap around
  for (int i = 4; i < 30; i++) s.push(T0 + i * 600, 1000.0f + i);
  CHECK(s.count == 10);
  CHECK(s.get(0, p) && std::fabs(p - 1029.0f) < 0.01f);
  CHECK(s.get(9, p) && std::fabs(p - 1020.0f) < 0.01f);
  // clock jumps back -> cleared
  s.push(T0, 1005.0f);
  CHECK(s.count == 1);
  // huge gap -> cleared
  s.push(T0 + 100000, 1005.0f);
  CHECK(s.count == 1);
}

static void testTrend() {
  Series<432> h;
  h.clear(600);
  // linear fall of 1 hPa/h for 6 h, with +-0.3 hPa noise
  for (int i = 0; i <= 36; i++) {
    float noise = (i % 3 == 0) ? 0.3f : ((i % 3 == 1) ? -0.3f : 0.0f);
    h.push(T0 + i * 600, 1015.0f - i / 6.0f + noise);
  }
  Trend t3 = trendOver(h, 3 * 3600);
  CHECK(t3.valid);
  NEAR(t3.change, -3.0f, 0.3f);
  CHECK(classify3h(t3) == TEND_NORMAL);
  Trend t12 = trendOver(h, 12 * 3600);
  CHECK(!t12.valid);  // only 6 h of data

  // gaps: remove most data -> invalid
  Series<432> g;
  g.clear(600);
  g.push(T0, 1010.0f);
  g.push(T0 + 3 * 3600, 1005.0f);
  CHECK(!trendOver(g, 3 * 3600).valid);
}

static void testJump() {
  Series<90> m;
  m.clear(60);
  for (int i = 0; i < 30; i++) m.push(T0 + i * 60, 1008.0f);
  float j;
  CHECK(pressureJump(m, 15, j));
  NEAR(j, 0.0f, 0.01f);
  m.push(T0 + 30 * 60, 1009.3f);
  m.push(T0 + 31 * 60, 1009.5f);
  CHECK(pressureJump(m, 15, j));
  NEAR(j, 1.4f, 0.02f);
}

static Metrics metrics3h(float c3) {
  Metrics m;
  m.set(M_3H, Trend{true, c3});
  return m;
}

static void testAlarmLevels() {
  AlarmConfig cfg;
  AlarmState st;
  st.clear();
  evaluateAlarms(metrics3h(-1.0f), cfg, st, T0);
  CHECK(st.level == LVL_NONE);
  evaluateAlarms(metrics3h(-2.2f), cfg, st, T0);
  CHECK(st.level == LVL_ADVISORY && st.primary == R_FALL3H_ADVISORY);
  CHECK(alarmUnacknowledged(st));
  acknowledge(st);
  CHECK(!alarmUnacknowledged(st));
  evaluateAlarms(metrics3h(-4.0f), cfg, st, T0);
  CHECK(st.level == LVL_STRONG && st.primary == R_FALL3H_STRONG);
  CHECK(alarmUnacknowledged(st));  // escalation sounds again
  acknowledge(st);
  // hysteresis: 3.0 is below 3.6 but above 3.6*0.75=2.7 -> stays STRONG
  evaluateAlarms(metrics3h(-3.0f), cfg, st, T0);
  CHECK(st.level == LVL_STRONG);
  CHECK(!alarmUnacknowledged(st));
  evaluateAlarms(metrics3h(-2.5f), cfg, st, T0);
  CHECK(st.level == LVL_ADVISORY);
  CHECK(st.ackLevel == LVL_ADVISORY);  // de-escalated, re-armed for STRONG
  evaluateAlarms(metrics3h(-6.5f), cfg, st, T0);
  CHECK(st.level == LVL_GALE && st.primary == R_FALL3H_GALE);
  evaluateAlarms(metrics3h(0.0f), cfg, st, T0);
  CHECK(st.level == LVL_NONE && st.ackLevel == LVL_NONE);
  // fast rise after a low
  evaluateAlarms(metrics3h(+5.0f), cfg, st, T0);
  CHECK(st.level == LVL_STRONG && st.primary == R_RISE3H_STRONG);
}

static void testSensitivityAndBomb() {
  AlarmConfig cfg;
  cfg.sensitivity = 0.75f;  // more sensitive
  AlarmState st;
  st.clear();
  evaluateAlarms(metrics3h(-3.0f), cfg, st, T0);
  CHECK(st.level == LVL_STRONG);  // 3.6*0.75 = 2.7

  AlarmConfig c2;
  c2.latitudeDeg = 52.0f;
  float thr = ruleThreshold(RULES[R_BOMB24H], c2);
  NEAR(thr, 21.8f, 0.2f);
  Metrics m;
  m.set(M_24H, Trend{true, -23.0f});
  AlarmState s2;
  s2.clear();
  evaluateAlarms(m, c2, s2, T0);
  CHECK(s2.level == LVL_GALE && s2.primary == R_BOMB24H);
}

static void testSquall() {
  AlarmConfig cfg;
  AlarmState st;
  st.clear();
  Metrics m;
  m.valid[M_JUMP] = true;
  m.value[M_JUMP] = 1.3f;
  evaluateAlarms(m, cfg, st, T0);
  CHECK(st.level == LVL_STRONG && st.primary == R_JUMP_SQUALL);
}

// End-to-end: simulated depression passage at 1 sample / 10 min.
static void testScenario() {
  Series<432> h;
  h.clear(600);
  AlarmConfig cfg;
  AlarmState st;
  st.clear();
  int firstStrong = -1, firstGale = -1;
  for (int i = 0; i < 24 * 6; i++) {
    float hours = i / 6.0f;
    // 1018 steady for 6h, then falling 1.5 hPa/h (falling quickly), bottom
    // at 18 h, then rising 2 hPa/h
    float p = 1018.0f;
    if (hours > 6) p -= (hours - 6) * 1.5f;
    if (hours > 14) p -= (hours - 14) * 1.0f;  // accelerating: 2.5 hPa/h
    if (hours > 18) p = 1018.0f - 12 - 10 + (hours - 18) * 2.0f;
    h.push(T0 + i * 600, p);
    Metrics m;
    m.set(M_3H, trendOver(h, 3 * 3600));
    m.set(M_12H, trendOver(h, 12 * 3600));
    m.set(M_24H, trendOver(h, 24 * 3600));
    evaluateAlarms(m, cfg, st, T0 + i * 600);
    if (st.level >= LVL_STRONG && firstStrong < 0) firstStrong = i;
    if (st.level >= LVL_GALE && firstGale < 0) firstGale = i;
  }
  std::printf("  scenario: STRONG after %.1f h, GALE after %.1f h\n", firstStrong / 6.0f,
              firstGale / 6.0f);
  CHECK(firstStrong > 6 * 6 && firstStrong < 12 * 6);
  CHECK(firstGale > 14 * 6 && firstGale < 18 * 6);
}

int main() {
  testEncode();
  testSeriesGaps();
  testTrend();
  testJump();
  testAlarmLevels();
  testSensitivityAndBomb();
  testSquall();
  testScenario();
  if (failures) {
    std::printf("%d check(s) FAILED\n", failures);
    return 1;
  }
  std::printf("all tests passed\n");
  return 0;
}
