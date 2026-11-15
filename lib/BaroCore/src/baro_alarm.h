// baro_alarm.h - weather-change alarm engine (hardware independent).
//
// Rules of thumb used (sea level, mid latitudes, North Sea / Channel):
//   * 3 h tendency (WMO / shipping forecast wording):
//       falling 2 hPa/3h            -> keep an eye on it, wind may increase
//       falling 3.6 hPa/3h "quickly"-> strong wind Bft 6-7 likely
//       falling 6 hPa/3h "very rapidly" -> gale Bft 8+ likely
//   * Fast rise after a low ("first rise after low foretells a stronger
//     blow") -> strong, gusty NW wind behind the cold front.
//   * 1 h fall >= 1.5 hPa -> front / trough is close.
//   * Pressure jump >= 1 hPa within ~15 min -> squall line / thunderstorm
//     gust front, sudden wind shift and gusts.
//   * 12 h fall >= 10 hPa -> deep depression approaching ("long foretold,
//     long last").
//   * 24 h fall >= 24 hPa * sin(lat)/sin(60) -> explosive cyclogenesis
//     ("weather bomb").
// These are indications only, never a replacement for the official forecast.
#pragma once
#include <math.h>
#include <stdint.h>
#include "baro_trend.h"

namespace baro {

enum Level : uint8_t { LVL_NONE = 0, LVL_ADVISORY = 1, LVL_STRONG = 2, LVL_GALE = 3 };

enum Metric : uint8_t { M_1H, M_3H, M_12H, M_24H, M_JUMP, M_COUNT };

enum RuleId : uint8_t {
  R_BOMB24H = 0,
  R_FALL3H_GALE,
  R_RISE3H_GALE,
  R_FALL1H_GALE,
  R_JUMP_SQUALL,
  R_FALL3H_STRONG,
  R_RISE3H_STRONG,
  R_FALL1H_STRONG,
  R_FALL12H_STRONG,
  R_FALL3H_ADVISORY,
  R_RISE3H_ADVISORY,
  R_COUNT,
  R_NONE = 0xFF
};

struct RuleDef {
  RuleId id;
  Metric metric;
  bool falling;     // true: alarm on falling pressure (value = -change)
  float threshold;  // hPa at sensitivity 1.0
  Level level;
};

// Order matters only for tie-breaking the "primary" reason: first wins.
static const RuleDef RULES[R_COUNT] = {
    {R_BOMB24H, M_24H, true, 24.0f, LVL_GALE},  // threshold latitude scaled
    {R_FALL3H_GALE, M_3H, true, 6.0f, LVL_GALE},
    {R_RISE3H_GALE, M_3H, false, 6.0f, LVL_GALE},
    {R_FALL1H_GALE, M_1H, true, 2.5f, LVL_GALE},
    {R_JUMP_SQUALL, M_JUMP, false, 1.0f, LVL_STRONG},
    {R_FALL3H_STRONG, M_3H, true, 3.6f, LVL_STRONG},
    {R_RISE3H_STRONG, M_3H, false, 4.5f, LVL_STRONG},
    {R_FALL1H_STRONG, M_1H, true, 1.5f, LVL_STRONG},
    {R_FALL12H_STRONG, M_12H, true, 10.0f, LVL_STRONG},
    {R_FALL3H_ADVISORY, M_3H, true, 2.0f, LVL_ADVISORY},
    {R_RISE3H_ADVISORY, M_3H, false, 3.0f, LVL_ADVISORY},
};

struct Metrics {
  bool valid[M_COUNT] = {false, false, false, false, false};
  float value[M_COUNT] = {0, 0, 0, 0, 0};  // hPa change (+ rising); jump >= 0
  void set(Metric m, const Trend &t) { valid[m] = t.valid; value[m] = t.change; }
};

struct AlarmConfig {
  float sensitivity = 1.0f;  // threshold multiplier: <1 more sensitive
  float latitudeDeg = 52.0f; // for the "bomb" criterion
  float releaseFactor = 0.75f; // hysteresis: rule clears below thr*factor
};

struct AlarmState {
  uint32_t activeMask;   // bit per RuleId
  uint8_t level;         // current overall level
  uint8_t ackLevel;      // level the crew acknowledged
  uint8_t primary;       // RuleId of main reason or R_NONE
  uint8_t pad;
  uint32_t sinceEpoch;   // when current level was entered
  uint32_t soundStartEpoch;
  void clear() {
    activeMask = 0; level = LVL_NONE; ackLevel = LVL_NONE; primary = R_NONE;
    pad = 0; sinceEpoch = 0; soundStartEpoch = 0;
  }
};

inline float ruleThreshold(const RuleDef &r, const AlarmConfig &cfg) {
  float thr = r.threshold;
  if (r.id == R_BOMB24H) {
    float lat = fabsf(cfg.latitudeDeg);
    if (lat < 25.0f) lat = 25.0f;  // criterion meaningless near the equator
    thr = 24.0f * sinf(lat * 3.14159265f / 180.0f) / sinf(60.0f * 3.14159265f / 180.0f);
  }
  return thr * cfg.sensitivity;
}

// Value a rule compares against its threshold (positive = alarming direction).
inline bool ruleValue(const RuleDef &r, const Metrics &m, float &v) {
  if (!m.valid[r.metric]) return false;
  v = r.falling ? -m.value[r.metric] : m.value[r.metric];
  return true;
}

// Re-evaluate all rules. Returns true when level or primary reason changed.
inline bool evaluateAlarms(const Metrics &m, const AlarmConfig &cfg, AlarmState &st,
                           uint32_t now) {
  uint32_t mask = 0;
  for (int i = 0; i < R_COUNT; i++) {
    const RuleDef &r = RULES[i];
    float v;
    bool was = (st.activeMask >> r.id) & 1u;
    if (!ruleValue(r, m, v)) {
      if (was) mask |= 1u << r.id;  // keep state while data is missing
      continue;
    }
    float thr = ruleThreshold(r, cfg);
    bool on = was ? (v >= thr * cfg.releaseFactor) : (v >= thr);
    if (on) mask |= 1u << r.id;
  }
  uint8_t level = LVL_NONE, primary = R_NONE;
  for (int i = 0; i < R_COUNT; i++) {
    const RuleDef &r = RULES[i];
    if (!((mask >> r.id) & 1u)) continue;
    if (r.level > level) { level = r.level; primary = r.id; }
  }
  bool changed = (level != st.level) || (primary != st.primary);
  if (level != st.level) st.sinceEpoch = now;
  st.activeMask = mask;
  st.level = level;
  st.primary = primary;
  if (st.ackLevel > level) st.ackLevel = level;  // de-escalation re-arms
  return changed;
}

inline bool alarmUnacknowledged(const AlarmState &st) {
  return st.level > LVL_NONE && st.level > st.ackLevel;
}

inline void acknowledge(AlarmState &st) { st.ackLevel = st.level; }

}  // namespace baro
