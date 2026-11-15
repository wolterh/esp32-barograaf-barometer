// display.h - draws the barograph screens on any Adafruit_GFX surface.
// The same code drives the e-paper (GxEPD2) and the PC preview tool.
#pragma once
#include <Adafruit_GFX.h>

#include "app_state.h"

void renderScreen(Adafruit_GFX &g, const AppState &s);

// Text of the main alarm reason, e.g. "Daling 3u -4.1 hPa (snel)".
void formatReason(char *buf, size_t len, uint8_t ruleId, const AppState &s);

// Local (display) calendar time helpers.
struct LocalTime {
  int year, month, day, hour, minute, second, wday;
};
LocalTime toLocal(uint32_t utcEpoch, int tzHours);
uint32_t fromLocal(const LocalTime &t, int tzHours);
