// config.h - hardware pins and tunables for the barograph.
//
// Board: Elecrow CrowPanel ESP32 4.2" E-Paper HMI (ESP32-S3-WROOM-1, N8R8),
// 400x300 black/white panel with SSD1683 controller.
// Pin numbers below follow Elecrow's example code for this board.
// CHECK them against your board revision before first use (see README).
#pragma once

// ---------------- E-paper (on-board, SPI) ----------------
#define EPD_PIN_PWR   7    // panel power switch, HIGH = on
#define EPD_PIN_MOSI  11
#define EPD_PIN_SCK   12
#define EPD_PIN_CS    45
#define EPD_PIN_DC    46
#define EPD_PIN_RST   47
#define EPD_PIN_BUSY  48

// ---------------- Buttons (on-board, active LOW) ----------------
// All are RTC GPIOs on the ESP32-S3, so they can wake it from deep sleep.
#define BTN_HOME  2    // HOME  : back to main screen
#define BTN_EXIT  1    // EXIT  : open / close settings
#define BTN_PREV  6    // rotary switch up
#define BTN_NEXT  4    // rotary switch down
#define BTN_OK    5    // rotary switch press: acknowledge alarm / edit

// ---------------- I2C bus (GPIO header) ----------------
// Barometer (BME280 or BMP390) and DS3231 RTC share this bus.
#define I2C_SDA   15
#define I2C_SCL   16
#define I2C_FREQ  100000

// ---------------- Pressure sensor ----------------
// Select exactly one. BMP390 is more accurate (+-0.03 hPa relative),
// BME280 also gives temperature/humidity of the cabin.
#define SENSOR_BME280 1
// #define SENSOR_BMP390 1
#define SENSOR_I2C_ADDR 0x76  // 0x77 when SDO is pulled high

// ---------------- Alarm outputs ----------------
#define BUZZER_PIN        14   // active buzzer (via transistor/MOSFET), HIGH = sound
#define BUZZER_ACTIVE_HIGH 1
#define LED_PIN           41   // alarm LED, -1 to disable

// ---------------- Timing ----------------
#define MEASURE_INTERVAL_S   60     // one pressure reading per minute
#define HISTORY_INTERVAL_S   600    // one stored barograph point per 10 min
#define HISTORY_SLOTS        432    // 72 h at 10 min
#define MINUTE_SLOTS         90     // 90 min at 1 min (1 h trend, squalls)
#define PERSIST_EVERY_S      3600   // write history to flash every hour
#define UI_TIMEOUT_S         45     // stay awake after last key press
#define ALARM_SOUND_S        90     // continuous alarm before going to reminders
#define FULL_REFRESH_EVERY   10     // partial updates between full refreshes (UI)
#define SAMPLES_PER_READING  4      // averaged sensor readings per measurement

// ---------------- Defaults for settings (changeable on the device) -------
#define DEFAULT_LATITUDE     52     // degrees, used for the "bomb" criterion
#define DEFAULT_TZ_HOURS     1      // display time = UTC + this (RTC keeps UTC)
#define DEFAULT_ALTITUDE_M   2      // sensor height above the water line
#define DEFAULT_LANG         0      // 0 = Nederlands, 1 = English
