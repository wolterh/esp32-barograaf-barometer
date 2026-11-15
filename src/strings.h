// strings.h - user interface texts (Dutch / English).
// Adafruit GFX fonts are 7-bit ASCII: no accents or degree sign.
#pragma once
#include "baro_alarm.h"
#include "baro_trend.h"

struct LangText {
  const char *tendency[6];        // index baro::TendencyClass
  const char *rising, *falling;
  const char *level[4];           // banner title per level
  const char *reason[baro::R_COUNT];
  const char *windHint[4];
  const char *now, *hoursSuffix;
  const char *noData, *noSensor, *timeUnknown;
  const char *ackHint;
  const char *screenLong, *screenTrend, *screenSettings;
  const char *trendTableHead;
  const char *activeRules, *noActiveRules;
  const char *rulesOfThumb[3];
  const char *setting[16];
  const char *sens[3];
  const char *onOff[2];
  const char *langName[2];
  const char *updated;
  const char *disclaimer;
  const char *testAlarm;
  const char *pressOk;
};

static const LangText LANG_NL = {
    {"?", "gelijk", "langzaam", "", "snel", "zeer snel"},
    "stijgend",
    "dalend",
    {"", "LET OP: DRUK VERANDERT", "HARDE WIND VERWACHT", "STORM VERWACHT"},
    {
        "Weerbom: >%.0f hPa daling in 24u",
        "Daling 3u %.1f hPa (zeer snel)",
        "Stijging 3u +%.1f hPa (zeer snel)",
        "Daling 1u %.1f hPa: front dichtbij",
        "Druksprong +%.1f hPa: buienlijn",
        "Daling 3u %.1f hPa (snel)",
        "Stijging 3u +%.1f hPa na depressie",
        "Daling 1u %.1f hPa: front nadert",
        "Daling 12u %.1f hPa: diepe depressie",
        "Daling 3u %.1f hPa",
        "Stijging 3u +%.1f hPa",
    },
    {"", "Wind kan toenemen (Bft 5-6)", "Bft 6-7 waarschijnlijk", "Bft 8+ mogelijk, reef!"},
    "nu",
    "u",
    "geen data",
    "SENSOR FOUT",
    "TIJD?",
    "OK = alarm bevestigen",
    "Barograaf 72 uur",
    "Tendens",
    "Instellingen",
    "periode  verandering  tendens",
    "Actieve waarschuwingen:",
    "Geen waarschuwingen actief.",
    {"Snelle daling/stijging = veel wind, kort.",
     "Lang aangekondigd, lang van duur.",
     "Eerste stijging na laag: zwaardere wind."},
    {"Gevoeligheid", "Kalibratie hPa", "Hoogte sensor m", "Breedtegraad", "Tijdzone UTC+",
     "Zoemer", "Slaapstand", "Taal", "Jaar", "Maand", "Dag", "Uur (lokaal)", "Minuut",
     "Test alarm", "Wis historie", ""},
    {"gevoelig", "normaal", "ruim"},
    {"uit", "aan"},
    {"Nederlands", "English"},
    "bijgewerkt",
    "Indicatie, geen vervanging van weerbericht.",
    "TEST ALARM",
    "OK = uitvoeren",
};

static const LangText LANG_EN = {
    {"?", "steady", "slowly", "", "quickly", "very rapidly"},
    "rising",
    "falling",
    {"", "CAUTION: PRESSURE CHANGE", "STRONG WIND LIKELY", "GALE LIKELY"},
    {
        "Weather bomb: >%.0f hPa fall in 24h",
        "3h fall %.1f hPa (very rapidly)",
        "3h rise +%.1f hPa (very rapidly)",
        "1h fall %.1f hPa: front close",
        "Pressure jump +%.1f hPa: squall line",
        "3h fall %.1f hPa (quickly)",
        "3h rise +%.1f hPa after low",
        "1h fall %.1f hPa: front approaching",
        "12h fall %.1f hPa: deep low",
        "3h fall %.1f hPa",
        "3h rise +%.1f hPa",
    },
    {"", "Wind may increase (Bft 5-6)", "Bft 6-7 likely", "Bft 8+ possible, reef!"},
    "now",
    "h",
    "no data",
    "SENSOR FAULT",
    "TIME?",
    "OK = acknowledge alarm",
    "Barograph 72 hours",
    "Tendency",
    "Settings",
    "period   change      tendency",
    "Active warnings:",
    "No warnings active.",
    {"Fast fall/rise = strong wind, short-lived.",
     "Long foretold, long last.",
     "First rise after low: stronger blow."},
    {"Sensitivity", "Calibration hPa", "Sensor height m", "Latitude", "Time zone UTC+",
     "Buzzer", "Deep sleep", "Language", "Year", "Month", "Day", "Hour (local)", "Minute",
     "Test alarm", "Clear history", ""},
    {"sensitive", "normal", "relaxed"},
    {"off", "on"},
    {"Nederlands", "English"},
    "updated",
    "Indication only, not a forecast.",
    "ALARM TEST",
    "OK = execute",
};

inline const LangText &langText(uint8_t lang) { return lang == 1 ? LANG_EN : LANG_NL; }
