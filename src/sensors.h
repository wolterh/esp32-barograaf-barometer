// sensors.h - pressure sensor and DS3231 real-time clock.
#pragma once
#include <stdint.h>

struct Reading {
  bool ok = false;
  float pressure = 0;   // station pressure, hPa
  bool haveTemp = false;
  float tempC = 0;
  float humidity = -1;  // %RH, < 0 if not available
};

bool sensorBegin();
Reading sensorRead();  // averaged forced-mode measurement

// RTC: keeps UTC. Falls back to the ESP32 internal clock when absent.
bool rtcBegin();
bool rtcPresent();
bool rtcTimeValid();          // false after battery loss until time is set
uint32_t rtcNowUtc();         // epoch seconds
void rtcSetUtc(uint32_t epoch);
float rtcTemperature();       // DS3231 die temperature, NAN if absent
