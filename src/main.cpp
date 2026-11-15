// Barograph / storm alarm for a sailing yacht.
// Elecrow CrowPanel ESP32-S3 4.2" e-paper + BME280/BMP390 + DS3231 RTC.
//
// Cycle (deep sleep enabled):
//   wake every minute -> read pressure -> update 1-min and 10-min series
//   -> evaluate alarm rules -> sound buzzer if needed -> refresh the e-paper
//   every 10 minutes (or on alarm change / key press) -> deep sleep.
// Any key wakes the device for UI use; the first key press while an alarm
// is unacknowledged acknowledges it.
#include <Arduino.h>
#include <GxEPD2_BW.h>
#include <SPI.h>
#include <Wire.h>
#include <driver/rtc_io.h>
#include <esp_sleep.h>

#include "../include/config.h"
#include "app_state.h"
#include "display.h"
#include "sensors.h"
#include "storage.h"
#include "strings.h"

using namespace baro;

// ------------------------------------------------------------------ state
// Kept in RTC slow memory: survives deep sleep (not a power cut; the
// 10-min history is additionally saved to flash every hour).
static const uint32_t RTC_MAGIC = 0x0BA20C01;
struct RtcMem {
  uint32_t magic;
  History hist;            // station pressure, 10-min slots, 72 h
  MinuteSeries minutes;    // station pressure, 1-min slots, 90 min
  AlarmState alarm;
  Settings settings;
  double accSum;           // running mean of the current 10-min slot
  uint16_t accCount;
  uint32_t accSlot;
  uint32_t nextMeasure;    // epoch of next scheduled reading
  uint32_t lastPersist;
  uint32_t lastFullRefresh;
  uint32_t lastRenderSlot; // history slot shown on the display
  uint8_t lastRenderLevel;
  uint8_t advisoryChirped;
  float lastTemp, lastHum;
  uint8_t haveTemp;
  uint8_t sensorOk;
  uint8_t shownSensorOk;   // sensor state currently on the display
};
RTC_DATA_ATTR static RtcMem M;

static AppState ui;
static GxEPD2_BW<GxEPD2_420_GDEY042T81, GxEPD2_420_GDEY042T81::HEIGHT> display(
    GxEPD2_420_GDEY042T81(EPD_PIN_CS, EPD_PIN_DC, EPD_PIN_RST, EPD_PIN_BUSY));
static bool displayReady = false;
static bool displayDirty = false;
static bool wantFullRefresh = false;
static uint16_t partialCount = 0;

static uint32_t lastKeyMs = 0;
static bool uiSession = false;
static uint32_t testAlarmUntilMs = 0;
static String serialLine;

static const uint8_t KEYS[] = {BTN_HOME, BTN_EXIT, BTN_PREV, BTN_NEXT, BTN_OK};
static const int NKEYS = sizeof(KEYS);
static bool keyDown[NKEYS];
static uint32_t keyChangeMs[NKEYS];

// ------------------------------------------------------------------ helpers
static float correction() {
  // ~0.12 hPa per metre above sea level + user calibration
  return M.settings.altitudeM * 0.12f + M.settings.offsetDeci / 10.0f;
}

static AlarmConfig alarmConfig() {
  AlarmConfig c;
  c.sensitivity = sensitivityFactor(M.settings.sensitivity);
  c.latitudeDeg = M.settings.latitude;
  return c;
}

static void outputs(bool on) {
  digitalWrite(BUZZER_PIN, (on && M.settings.buzzer) == BUZZER_ACTIVE_HIGH ? HIGH : LOW);
  if (LED_PIN >= 0) digitalWrite(LED_PIN, on ? HIGH : LOW);
}

static void beep(int count, int onMs, int offMs) {
  for (int i = 0; i < count; i++) {
    outputs(true);
    delay(onMs);
    outputs(false);
    if (i + 1 < count) delay(offMs);
  }
}

static void coldStartState() {
  memset(&M, 0, sizeof M);
  M.magic = RTC_MAGIC;
  M.hist.clear(HISTORY_INTERVAL_S);
  M.minutes.clear(60);
  M.alarm.clear();
  loadSettings(M.settings);
  loadHistory(M.hist);  // gaps since the power cut are filled on next push
  M.lastRenderSlot = 0xFFFFFFFF;
}

// Build the view model used by the renderer.
static void refreshViewModel(uint32_t now) {
  ui.hist = &M.hist;
  ui.minutes = &M.minutes;
  ui.alarm = &M.alarm;
  ui.settings = &M.settings;
  ui.nowUtc = now;
  ui.timeValid = rtcTimeValid();
  ui.sensorOk = M.sensorOk;
  ui.correction = correction();
  float p;
  ui.havePressure = recentMean(M.minutes, 3, p) && M.minutes.staleSlots(now) < 5;
  ui.pressure = p + ui.correction;
  ui.haveTemp = M.haveTemp;
  ui.tempC = M.lastTemp;
  ui.humidity = M.lastHum;

  Metrics m;
  m.set(M_1H, trendOver(M.minutes, 3600));
  m.set(M_3H, trendOver(M.hist, 3 * 3600));
  m.set(M_12H, trendOver(M.hist, 12 * 3600));
  m.set(M_24H, trendOver(M.hist, 24 * 3600));
  float jump;
  if (pressureJump(M.minutes, 15, jump)) {
    m.valid[M_JUMP] = true;
    m.value[M_JUMP] = jump;
  }
  ui.metrics = m;
  ui.trend6h = trendOver(M.hist, 6 * 3600);
  ui.testAlarm = testAlarmUntilMs && millis() < testAlarmUntilMs;
}

// ------------------------------------------------------------------ display
static void displayBegin() {
  if (displayReady) return;
  pinMode(EPD_PIN_PWR, OUTPUT);
  digitalWrite(EPD_PIN_PWR, HIGH);
  delay(10);
  SPI.begin(EPD_PIN_SCK, -1, EPD_PIN_MOSI, EPD_PIN_CS);
  display.epd2.selectSPI(SPI, SPISettings(4000000, MSBFIRST, SPI_MODE0));
  display.init(0, true, 10, false);  // panel power was cut: start with a full refresh
  display.setRotation(0);
  displayReady = true;
  partialCount = 0;
}

static void render(uint32_t now) {
  displayBegin();
  refreshViewModel(now);
  bool full = wantFullRefresh || partialCount >= FULL_REFRESH_EVERY || !uiSession;
  if (full) {
    display.setFullWindow();
    partialCount = 0;
    M.lastFullRefresh = now;
  } else {
    display.setPartialWindow(0, 0, display.width(), display.height());
    partialCount++;
  }
  display.firstPage();
  do {
    renderScreen(display, ui);
  } while (display.nextPage());
  displayDirty = false;
  wantFullRefresh = false;
  M.lastRenderSlot = M.hist.slotOf(now);
  M.lastRenderLevel = M.alarm.level;
  M.shownSensorOk = M.sensorOk;
}

// ------------------------------------------------------------------ measuring
static void measure(uint32_t now) {
  Reading r = sensorRead();
  M.sensorOk = r.ok;
  if (r.ok) {
    M.minutes.push(now, r.pressure);
    uint32_t slot = M.hist.slotOf(now);
    if (slot != M.accSlot) {
      M.accSlot = slot;
      M.accSum = 0;
      M.accCount = 0;
    }
    M.accSum += r.pressure;
    M.accCount++;
    M.hist.push(now, (float)(M.accSum / M.accCount));  // same slot overwrites
    M.haveTemp = r.haveTemp;
    M.lastTemp = r.tempC;
    M.lastHum = r.humidity;
  }
  refreshViewModel(now);
  bool changed = evaluateAlarms(ui.metrics, alarmConfig(), M.alarm, now);
  if (M.alarm.level == LVL_NONE) M.advisoryChirped = 0;
  if (changed) displayDirty = true;
  if (M.hist.slotOf(now) != M.lastRenderSlot) displayDirty = true;  // every 10 min
  if (M.sensorOk != M.shownSensorOk) displayDirty = true;  // fault appears/clears
  if (now - M.lastPersist >= PERSIST_EVERY_S) {
    saveHistory(M.hist);
    M.lastPersist = now;
  }
  M.nextMeasure = now - (now % MEASURE_INTERVAL_S) + MEASURE_INTERVAL_S;
}

// ------------------------------------------------------------------ alarm sound
// Returns true while the alarm needs the CPU awake (continuous sounding).
static bool alarmTick(uint32_t now, bool freshMeasurement) {
  if (ui.testAlarm) {
    outputs((millis() % 400) < 200);
    return true;
  }
  if (!alarmUnacknowledged(M.alarm)) {
    M.alarm.soundStartEpoch = 0;
    outputs(false);
    return false;
  }
  if (M.alarm.level == LVL_ADVISORY) {
    // advisory: two short chirps once, then it is considered seen
    if (!M.advisoryChirped) {
      beep(2, 80, 120);
      M.advisoryChirped = 1;
      acknowledge(M.alarm);
      displayDirty = true;
    }
    return false;
  }
  if (M.alarm.soundStartEpoch == 0) M.alarm.soundStartEpoch = now;
  if (now - M.alarm.soundStartEpoch < ALARM_SOUND_S) {
    uint32_t period = M.alarm.level >= LVL_GALE ? 400 : 1500;
    uint32_t onMs = M.alarm.level >= LVL_GALE ? 200 : 500;
    outputs((millis() % period) < onMs);
    return true;
  }
  // after the continuous phase: a short reminder every minute until acked
  if (freshMeasurement) beep(M.alarm.level >= LVL_GALE ? 3 : 2, 150, 150);
  return false;
}

// ------------------------------------------------------------------ settings UI
static int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
static int wrapi(int v, int lo, int hi) {
  int n = hi - lo + 1;
  return lo + ((v - lo) % n + n) % n;
}
static int daysInMonth(int y, int m) {
  static const int d[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  bool leap = (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
  return m == 2 && leap ? 29 : d[(m - 1) % 12];
}

static void adjustSetting(uint8_t item, int d, uint32_t now) {
  Settings &s = M.settings;
  int tz = s.tzHours;
  LocalTime lt = toLocal(now, tz);
  bool setClock = false;
  switch (item) {
    case SET_SENSITIVITY: s.sensitivity = (uint8_t)wrapi(s.sensitivity + d, 0, 2); break;
    case SET_OFFSET: s.offsetDeci = (int16_t)clampi(s.offsetDeci + d, -150, 150); break;
    case SET_ALTITUDE: s.altitudeM = (int16_t)clampi(s.altitudeM + d, 0, 100); break;
    case SET_LATITUDE: s.latitude = (int8_t)clampi(s.latitude + d, -80, 80); break;
    case SET_TZ: s.tzHours = (int8_t)clampi(s.tzHours + d, -12, 14); break;
    case SET_BUZZER: s.buzzer = !s.buzzer; break;
    case SET_SLEEP: s.sleep = !s.sleep; break;
    case SET_LANG: s.lang = !s.lang; break;
    case SET_YEAR: lt.year = clampi(lt.year + d, 2024, 2099); setClock = true; break;
    case SET_MONTH: lt.month = wrapi(lt.month + d, 1, 12); setClock = true; break;
    case SET_DAY: lt.day = wrapi(lt.day + d, 1, daysInMonth(lt.year, lt.month)); setClock = true; break;
    case SET_HOUR: lt.hour = wrapi(lt.hour + d, 0, 23); setClock = true; break;
    case SET_MINUTE: lt.minute = wrapi(lt.minute + d, 0, 59); lt.second = 0; setClock = true; break;
    default: break;
  }
  if (setClock) {
    lt.day = clampi(lt.day, 1, daysInMonth(lt.year, lt.month));
    rtcSetUtc(fromLocal(lt, tz));
    M.nextMeasure = 0;  // re-align the measuring schedule
  }
}

static void clearHistory() {
  M.hist.clear(HISTORY_INTERVAL_S);
  M.minutes.clear(60);
  M.alarm.clear();
  M.accCount = 0;
  M.accSlot = 0;
  saveHistory(M.hist);
}

static void startAlarmTest() {
  testAlarmUntilMs = millis() + 8000;
  displayDirty = true;
}

static void onKey(int k, uint32_t now) {
  lastKeyMs = millis();
  uiSession = true;
  // any key first acknowledges an alarm (or stops the test)
  if (testAlarmUntilMs) {
    testAlarmUntilMs = 0;
    outputs(false);
    displayDirty = true;
    return;
  }
  if (alarmUnacknowledged(M.alarm)) {
    acknowledge(M.alarm);
    outputs(false);
    displayDirty = true;
    return;
  }
  uint8_t key = KEYS[k];
  if (ui.screen == SCR_SETTINGS) {
    if (key == BTN_EXIT || key == BTN_HOME) {
      saveSettings(M.settings);
      ui.screen = SCR_MAIN;
      ui.editing = false;
      wantFullRefresh = true;
    } else if (key == BTN_OK) {
      if (ui.cursor == SET_ALARM_TEST) startAlarmTest();
      else if (ui.cursor == SET_CLEAR_HISTORY) clearHistory();
      else if (ui.cursor == SET_BUZZER || ui.cursor == SET_SLEEP || ui.cursor == SET_LANG)
        adjustSetting(ui.cursor, 1, now);
      else ui.editing = !ui.editing;
    } else {
      int d = key == BTN_NEXT ? 1 : -1;
      if (ui.editing) adjustSetting(ui.cursor, d, now);
      else ui.cursor = (uint8_t)wrapi(ui.cursor + d, 0, SET_COUNT - 1);
    }
  } else {
    if (key == BTN_NEXT) ui.screen = (Screen)wrapi(ui.screen + 1, 0, SCR_SETTINGS - 1);
    else if (key == BTN_PREV) ui.screen = (Screen)wrapi(ui.screen - 1, 0, SCR_SETTINGS - 1);
    else if (key == BTN_HOME) ui.screen = SCR_MAIN;
    else if (key == BTN_EXIT) { ui.screen = SCR_SETTINGS; ui.cursor = 0; ui.editing = false; }
    else if (key == BTN_OK) wantFullRefresh = true;  // clean redraw
  }
  displayDirty = true;
}

static void pollKeys(uint32_t now) {
  uint32_t ms = millis();
  for (int i = 0; i < NKEYS; i++) {
    bool down = digitalRead(KEYS[i]) == LOW;
    if (down != keyDown[i] && ms - keyChangeMs[i] > 40) {
      keyDown[i] = down;
      keyChangeMs[i] = ms;
      if (down) onKey(i, now);
    }
  }
}

// ------------------------------------------------------------------ serial
static void printStatus(uint32_t now) {
  refreshViewModel(now);
  LocalTime lt = toLocal(now, M.settings.tzHours);
  Serial.printf("time   %04d-%02d-%02d %02d:%02d:%02d local (UTC%+d) %s, rtc %s\n", lt.year,
                lt.month, lt.day, lt.hour, lt.minute, lt.second, M.settings.tzHours,
                rtcTimeValid() ? "valid" : "NOT SET", rtcPresent() ? "DS3231" : "internal");
  Serial.printf("sensor %s  p=%.2f hPa (corr %+.2f)\n", M.sensorOk ? "ok" : "FAULT", ui.pressure,
                ui.correction);
  const char *names[] = {"1h", "3h", "12h", "24h", "jump15"};
  for (int i = 0; i < M_COUNT; i++) {
    if (ui.metrics.valid[i]) Serial.printf("  %-6s %+.2f hPa\n", names[i], ui.metrics.value[i]);
    else Serial.printf("  %-6s n/a\n", names[i]);
  }
  Serial.printf("alarm level %u ack %u primary %u mask 0x%03lx\n", M.alarm.level, M.alarm.ackLevel,
                M.alarm.primary, (unsigned long)M.alarm.activeMask);
  Serial.printf("history %u slots, minutes %u slots\n", M.hist.count, M.minutes.count);
}

static void dumpHistory() {
  Serial.println("epoch_utc,hPa");
  float c = correction();
  for (int ago = M.hist.count - 1; ago >= 0; ago--) {
    float p;
    uint32_t t = M.hist.newestEpoch - (uint32_t)ago * HISTORY_INTERVAL_S;
    if (M.hist.get(ago, p)) Serial.printf("%lu,%.2f\n", (unsigned long)t, p + c);
    else Serial.printf("%lu,\n", (unsigned long)t);
  }
}

// Fill the history with a synthetic depression passage (demo / test).
static void simulate(uint32_t now) {
  M.hist.clear(HISTORY_INTERVAL_S);
  M.minutes.clear(60);
  M.alarm.clear();
  float c = correction();
  for (int i = HISTORY_SLOTS - 1; i >= 0; i--) {
    float hAgo = i * HISTORY_INTERVAL_S / 3600.0f;
    float p = 1021.0f + 1.5f * sinf(hAgo / 7.0f);          // quiet high
    if (hAgo < 12) p -= (12 - hAgo) * 1.3f;                // falling for 12 h
    if (hAgo < 3) p -= (3 - hAgo) * 0.6f;                  // accelerating
    M.hist.push(now - (uint32_t)i * HISTORY_INTERVAL_S, p - c);
  }
  float last;
  M.hist.get(0, last);
  for (int i = MINUTE_SLOTS - 1; i >= 0; i--)
    M.minutes.push(now - i * 60, last + i * (1.9f / 60.0f));
  M.accSlot = M.hist.slotOf(now);
  M.accSum = last;
  M.accCount = 1;
  saveHistory(M.hist);
  refreshViewModel(now);
  evaluateAlarms(ui.metrics, alarmConfig(), M.alarm, now);
  wantFullRefresh = true;
  displayDirty = true;
}

static bool parseTime(const String &s, uint32_t &out) {
  LocalTime t{};
  if (sscanf(s.c_str(), "%d-%d-%d %d:%d:%d", &t.year, &t.month, &t.day, &t.hour, &t.minute,
             &t.second) < 5)
    return false;
  if (t.year < 2024 || t.month < 1 || t.month > 12 || t.day < 1 || t.day > 31) return false;
  out = fromLocal(t, 0);
  return true;
}

static void handleSerialLine(String line, uint32_t now) {
  line.trim();
  String up = line;
  up.toUpperCase();
  lastKeyMs = millis();
  uiSession = true;
  if (up.startsWith("TIME ")) {
    uint32_t t;
    if (parseTime(line.substring(5), t)) {
      rtcSetUtc(t);
      M.nextMeasure = 0;
      Serial.println("ok, RTC set (UTC)");
      displayDirty = true;
    } else {
      Serial.println("usage: TIME YYYY-MM-DD HH:MM:SS   (UTC)");
    }
  } else if (up == "STATUS") {
    printStatus(now);
  } else if (up == "DUMP") {
    dumpHistory();
  } else if (up == "SIM") {
    simulate(now);
    Serial.println("ok, demo depression loaded");
  } else if (up == "CLEAR") {
    clearHistory();
    displayDirty = true;
    Serial.println("ok, history cleared");
  } else if (up == "TEST") {
    startAlarmTest();
    Serial.println("alarm test (8 s)");
  } else if (up == "SAVE") {
    saveHistory(M.hist);
    saveSettings(M.settings);
    Serial.println("ok");
  } else {
    Serial.println("commands: STATUS | TIME YYYY-MM-DD HH:MM:SS | DUMP | SIM | CLEAR | TEST | SAVE");
  }
}

static void pollSerial(uint32_t now) {
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\n' || c == '\r') {
      if (serialLine.length()) handleSerialLine(serialLine, now);
      serialLine = "";
    } else if (serialLine.length() < 64) {
      serialLine += c;
    }
  }
}

// ------------------------------------------------------------------ sleep
static void goToSleep(uint32_t now) {
  // wait until no key is held, otherwise ext1 wakes us immediately
  uint32_t t0 = millis();
  bool held;
  do {
    held = false;
    for (int i = 0; i < NKEYS; i++) held |= digitalRead(KEYS[i]) == LOW;
    if (held) delay(20);
  } while (held && millis() - t0 < 3000);

  outputs(false);
  if (displayReady) display.hibernate();
  digitalWrite(EPD_PIN_PWR, LOW);

  gpio_hold_en((gpio_num_t)BUZZER_PIN);  // keep buzzer driver off while asleep
  gpio_deep_sleep_hold_en();

  uint64_t mask = 0;
  for (int i = 0; i < NKEYS; i++) {
    gpio_num_t g = (gpio_num_t)KEYS[i];
    mask |= 1ULL << KEYS[i];
    rtc_gpio_pullup_en(g);
    rtc_gpio_pulldown_dis(g);
  }
  esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);
  esp_sleep_enable_ext1_wakeup(mask, ESP_EXT1_WAKEUP_ANY_LOW);

  uint32_t secs = M.nextMeasure > now ? M.nextMeasure - now : 1;
  if (secs > MEASURE_INTERVAL_S) secs = MEASURE_INTERVAL_S;
  esp_sleep_enable_timer_wakeup((uint64_t)secs * 1000000ULL);
  Serial.flush();
  esp_deep_sleep_start();
}

// ------------------------------------------------------------------ main
void setup() {
  gpio_hold_dis((gpio_num_t)BUZZER_PIN);
  pinMode(BUZZER_PIN, OUTPUT);
  if (LED_PIN >= 0) pinMode(LED_PIN, OUTPUT);
  for (int i = 0; i < NKEYS; i++) {
    rtc_gpio_deinit((gpio_num_t)KEYS[i]);
    pinMode(KEYS[i], INPUT_PULLUP);
  }
  pinMode(EPD_PIN_PWR, OUTPUT);
  digitalWrite(EPD_PIN_PWR, LOW);

  Serial.begin(115200);
  Wire.begin(I2C_SDA, I2C_SCL, I2C_FREQ);
  storageBegin();

  bool warm = esp_reset_reason() == ESP_RST_DEEPSLEEP && M.magic == RTC_MAGIC;
  if (!warm) coldStartState();
  outputs(false);
  rtcBegin();
  sensorBegin();

  esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
  uint32_t now = rtcNowUtc();
  if (!warm) {
    Serial.println("\nBarograph start (cold). Type HELP for commands.");
    wantFullRefresh = true;
    displayDirty = true;
    M.nextMeasure = 0;
  }
  if (cause == ESP_SLEEP_WAKEUP_EXT1) {
    uint64_t st = esp_sleep_get_ext1_wakeup_status();
    for (int i = 0; i < NKEYS; i++) {
      if (st & (1ULL << KEYS[i])) {
        keyDown[i] = true;  // the release is debounced normally
        keyChangeMs[i] = millis();
        onKey(i, now);
      }
    }
  }
}

void loop() {
  static uint32_t lastRtcReadMs = 0;
  static uint32_t now = 0;
  uint32_t ms = millis();
  if (!now || ms - lastRtcReadMs >= 1000) {
    now = rtcNowUtc();
    lastRtcReadMs = ms;
  }

  pollSerial(now);
  pollKeys(now);

  bool fresh = false;
  if (now >= M.nextMeasure) {
    measure(now);
    fresh = true;
  }
  if (testAlarmUntilMs && ms >= testAlarmUntilMs) {
    testAlarmUntilMs = 0;
    outputs(false);
    displayDirty = true;
  }
  refreshViewModel(now);
  bool sounding = alarmTick(now, fresh);

  if (uiSession && ms - lastKeyMs > UI_TIMEOUT_S * 1000UL) {
    uiSession = false;
    if (ui.screen == SCR_SETTINGS) saveSettings(M.settings);
    ui.screen = SCR_MAIN;
    ui.editing = false;
    displayDirty = true;  // back to the main screen with a full refresh
  }
  if (displayDirty) render(now);

#if ARDUINO_USB_CDC_ON_BOOT
  bool usbHost = (bool)Serial;  // stay awake while a computer is connected
#else
  bool usbHost = false;
#endif
  if (M.settings.sleep && !uiSession && !sounding && !usbHost) goToSleep(now);
  delay(10);
}
