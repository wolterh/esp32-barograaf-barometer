// storage.cpp - history survives power loss; settings live in NVS.
#include "storage.h"

#include <Arduino.h>
#include <LittleFS.h>
#include <Preferences.h>

static const char *HIST_FILE = "/history.bin";
static const uint32_t HIST_MAGIC = 0xBA60A001;
static const uint16_t SETTINGS_VERSION = 1;
static bool fsOk = false;

struct HistHeader {
  uint32_t magic;
  uint16_t slots;
  uint16_t interval;
  uint32_t size;
};

void storageBegin() { fsOk = LittleFS.begin(true); }

static void defaults(Settings &s) {
  s.sensitivity = 1;
  s.offsetDeci = 0;
  s.altitudeM = DEFAULT_ALTITUDE_M;
  s.latitude = DEFAULT_LATITUDE;
  s.tzHours = DEFAULT_TZ_HOURS;
  s.buzzer = 1;
  s.sleep = 1;
  s.lang = DEFAULT_LANG;
}

void loadSettings(Settings &s) {
  defaults(s);
  Preferences p;
  if (!p.begin("barograph", true)) return;
  if (p.getUShort("ver", 0) == SETTINGS_VERSION &&
      p.getBytesLength("set") == sizeof(Settings)) {
    p.getBytes("set", &s, sizeof(Settings));
  }
  p.end();
  if (s.sensitivity > 2) s.sensitivity = 1;
  if (s.lang > 1) s.lang = 0;
}

void saveSettings(const Settings &s) {
  Preferences p;
  if (!p.begin("barograph", false)) return;
  p.putUShort("ver", SETTINGS_VERSION);
  p.putBytes("set", &s, sizeof(Settings));
  p.end();
}

bool loadHistory(History &h) {
  if (!fsOk) return false;
  File f = LittleFS.open(HIST_FILE, "r");
  if (!f) return false;
  HistHeader hd;
  bool ok = f.read((uint8_t *)&hd, sizeof hd) == sizeof hd && hd.magic == HIST_MAGIC &&
            hd.slots == HISTORY_SLOTS && hd.interval == HISTORY_INTERVAL_S &&
            hd.size == sizeof(History) &&
            f.read((uint8_t *)&h, sizeof(History)) == sizeof(History);
  f.close();
  if (ok && (h.head >= HISTORY_SLOTS || h.count > HISTORY_SLOTS)) ok = false;
  if (!ok) h.clear(HISTORY_INTERVAL_S);
  return ok;
}

bool saveHistory(const History &h) {
  if (!fsOk) return false;
  // write to a temp file first so a power cut never leaves a broken file
  const char *tmp = "/history.tmp";
  File f = LittleFS.open(tmp, "w");
  if (!f) return false;
  HistHeader hd = {HIST_MAGIC, HISTORY_SLOTS, HISTORY_INTERVAL_S, sizeof(History)};
  bool ok = f.write((const uint8_t *)&hd, sizeof hd) == sizeof hd &&
            f.write((const uint8_t *)&h, sizeof(History)) == sizeof(History);
  f.close();
  if (!ok) return false;
  LittleFS.remove(HIST_FILE);
  return LittleFS.rename(tmp, HIST_FILE);
}
