// app_state.h - everything the renderer needs, independent of the hardware.
#pragma once
#include <stdint.h>

#include "../include/config.h"
#include "baro_alarm.h"
#include "baro_series.h"
#include "baro_trend.h"

typedef baro::Series<HISTORY_SLOTS> History;
typedef baro::Series<MINUTE_SLOTS> MinuteSeries;

enum Screen : uint8_t { SCR_MAIN = 0, SCR_LONG, SCR_TREND, SCR_SETTINGS, SCR_COUNT };

enum SettingItem : uint8_t {
  SET_SENSITIVITY = 0,
  SET_OFFSET,
  SET_ALTITUDE,
  SET_LATITUDE,
  SET_TZ,
  SET_BUZZER,
  SET_SLEEP,
  SET_LANG,
  SET_YEAR,
  SET_MONTH,
  SET_DAY,
  SET_HOUR,
  SET_MINUTE,
  SET_ALARM_TEST,
  SET_CLEAR_HISTORY,
  SET_COUNT
};

struct Settings {
  uint8_t sensitivity;  // 0 = sensitive, 1 = normal, 2 = relaxed
  int16_t offsetDeci;   // calibration offset in 0.1 hPa
  int16_t altitudeM;    // sensor height above sea level
  int8_t latitude;      // degrees
  int8_t tzHours;       // display offset from UTC
  uint8_t buzzer;       // 1 = on
  uint8_t sleep;        // 1 = deep sleep between readings
  uint8_t lang;         // 0 = NL, 1 = EN
};

inline float sensitivityFactor(uint8_t s) {
  return s == 0 ? 0.75f : (s == 2 ? 1.3f : 1.0f);
}

struct AppState {
  // data
  const History *hist = nullptr;
  const MinuteSeries *minutes = nullptr;
  const baro::AlarmState *alarm = nullptr;
  const Settings *settings = nullptr;
  baro::Metrics metrics;
  baro::Trend trend6h;
  bool havePressure = false;
  float pressure = 0;  // sea-level corrected, calibrated (hPa)
  float correction = 0;  // added to stored station pressure for display
  bool haveTemp = false;
  float tempC = 0;
  float humidity = -1;  // < 0 when the sensor has none
  bool sensorOk = false;
  bool timeValid = false;
  uint32_t nowUtc = 0;  // epoch seconds
  // ui
  Screen screen = SCR_MAIN;
  uint8_t cursor = 0;
  bool editing = false;
  bool testAlarm = false;
};
