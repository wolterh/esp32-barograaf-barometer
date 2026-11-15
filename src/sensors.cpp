// sensors.cpp - BME280 / BMP390 barometer and DS3231 RTC on one I2C bus.
#include "sensors.h"

#include <Arduino.h>
#include <RTClib.h>
#include <Wire.h>
#include <math.h>
#include <sys/time.h>

#include "../include/config.h"

#if defined(SENSOR_BMP390)
#include <Adafruit_BMP3XX.h>
static Adafruit_BMP3XX bmp;
#else
#include <Adafruit_BME280.h>
static Adafruit_BME280 bme;
#endif

static bool sensorOk = false;

bool sensorBegin() {
#if defined(SENSOR_BMP390)
  sensorOk = bmp.begin_I2C(SENSOR_I2C_ADDR, &Wire);
  if (sensorOk) {
    bmp.setTemperatureOversampling(BMP3_OVERSAMPLING_2X);
    bmp.setPressureOversampling(BMP3_OVERSAMPLING_16X);
    bmp.setIIRFilterCoeff(BMP3_IIR_FILTER_DISABLE);  // we average ourselves
    bmp.setOutputDataRate(BMP3_ODR_25_HZ);
  }
#else
  sensorOk = bme.begin(SENSOR_I2C_ADDR, &Wire);
  if (sensorOk) {
    // Forced mode: the sensor sleeps between readings (no self heating).
    bme.setSampling(Adafruit_BME280::MODE_FORCED, Adafruit_BME280::SAMPLING_X2,
                    Adafruit_BME280::SAMPLING_X16, Adafruit_BME280::SAMPLING_X1,
                    Adafruit_BME280::FILTER_OFF);
  }
#endif
  return sensorOk;
}

static bool plausible(float hPa) { return hPa > 870.0f && hPa < 1090.0f; }

Reading sensorRead() {
  Reading r;
  if (!sensorOk && !sensorBegin()) return r;
  float sum = 0, tSum = 0, hSum = 0;
  int n = 0;
  for (int i = 0; i < SAMPLES_PER_READING; i++) {
#if defined(SENSOR_BMP390)
    if (!bmp.performReading()) continue;
    float p = bmp.pressure / 100.0f;
    float t = bmp.temperature;
    float h = -1;
#else
    if (!bme.takeForcedMeasurement()) continue;
    float t = bme.readTemperature();
    float p = bme.readPressure() / 100.0f;
    float h = bme.readHumidity();
#endif
    if (!plausible(p)) continue;
    sum += p; tSum += t; hSum += h;
    n++;
    delay(5);
  }
  if (n == 0) {
    sensorOk = false;  // try a re-init next time
    return r;
  }
  r.ok = true;
  r.pressure = sum / n;
  r.haveTemp = true;
  r.tempC = tSum / n;
  r.humidity = hSum / n;
#if defined(SENSOR_BMP390)
  r.humidity = -1;
#endif
  return r;
}

// ------------------------------------------------------------------ RTC
static RTC_DS3231 rtc;
static bool rtcOk = false;

static void syncSystemClock(uint32_t epoch) {
  struct timeval tv = {(time_t)epoch, 0};
  settimeofday(&tv, nullptr);
}

bool rtcBegin() {
  rtcOk = rtc.begin(&Wire);
  if (rtcOk) {
    rtc.disable32K();
    rtc.writeSqwPinMode(DS3231_OFF);
    syncSystemClock(rtc.now().unixtime());
  }
  return rtcOk;
}

bool rtcPresent() { return rtcOk; }

bool rtcTimeValid() {
  if (rtcOk) return !rtc.lostPower();
  // Without DS3231 we only trust the internal clock once it was set.
  return time(nullptr) > 1700000000;
}

uint32_t rtcNowUtc() {
  if (rtcOk) {
    uint32_t t = rtc.now().unixtime();
    if (t > 946684800UL) return t;  // sanity: after 2000-01-01
  }
  return (uint32_t)time(nullptr);  // ESP32 RTC timer keeps running in deep sleep
}

void rtcSetUtc(uint32_t epoch) {
  if (rtcOk) rtc.adjust(DateTime(epoch));  // also clears the lost-power flag
  syncSystemClock(epoch);
}

float rtcTemperature() { return rtcOk ? rtc.getTemperature() : NAN; }
