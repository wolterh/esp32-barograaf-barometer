// baro_trend.h - pressure tendency calculations (hardware independent).
#pragma once
#include <stdint.h>
#include "baro_series.h"

namespace baro {

struct Trend {
  bool valid;
  float change;  // hPa over the window, positive = rising
  Trend() : valid(false), change(0.0f) {}
  Trend(bool v, float c) : valid(v), change(c) {}
};

// Least-squares slope over the last `windowS` seconds, expressed as the
// change over the whole window. A regression is used instead of a plain
// "now minus then" difference because on a moving yacht the sensor sees
// gusts, door slams and hatch openings; a fit over all samples is far less
// noisy. Requires the window to be at least `minCoverage` filled and data
// near both ends of the window.
template <int N>
Trend trendOver(const Series<N> &s, uint32_t windowS, float minCoverage = 0.6f) {
  Trend t;
  if (s.count < 2 || s.intervalS == 0) return t;
  int slots = (int)(windowS / s.intervalS);
  if (slots < 2) return t;
  if (slots > s.count - 1) slots = s.count - 1;
  if (slots < 2) return t;

  double sx = 0, sy = 0, sxx = 0, sxy = 0;
  int n = 0, newestAgo = -1, oldestAgo = -1;
  for (int ago = 0; ago <= slots; ago++) {
    float p;
    if (!s.get(ago, p)) continue;
    double x = -(double)ago * s.intervalS;  // seconds relative to newest
    sx += x; sy += p; sxx += x * x; sxy += x * p;
    n++;
    if (newestAgo < 0) newestAgo = ago;
    oldestAgo = ago;
  }
  int wanted = (int)(windowS / s.intervalS) + 1;
  if (n < 3 || n < (int)(minCoverage * wanted)) return t;
  // data must reach both ends of the requested window
  int endTol = wanted / 5 + 1;
  if (newestAgo > endTol) return t;
  if (oldestAgo < (wanted - 1) - endTol) return t;
  double den = n * sxx - sx * sx;
  if (den <= 0) return t;
  double slope = (n * sxy - sx * sy) / den;  // hPa per second
  t.valid = true;
  t.change = (float)(slope * (double)windowS);
  return t;
}

// Mean of the newest `slots` valid samples (used as "current" pressure).
template <int N>
bool recentMean(const Series<N> &s, int slots, float &out) {
  double sum = 0;
  int n = 0;
  for (int ago = 0; ago < slots && ago < s.count; ago++) {
    float p;
    if (s.get(ago, p)) { sum += p; n++; }
  }
  if (!n) return false;
  out = (float)(sum / n);
  return true;
}

// Pressure jump detector (squall line / thunderstorm gust front): the rise of
// the recent mean (last 2 slots) above the lowest value in the preceding
// `lookbackSlots`. Returns false when there is not enough data.
template <int N>
bool pressureJump(const Series<N> &s, int lookbackSlots, float &jump) {
  float now;
  if (!recentMean(s, 2, now)) return false;
  float lo = 1e9f;
  int n = 0;
  for (int ago = 2; ago <= lookbackSlots && ago < s.count; ago++) {
    float p;
    if (s.get(ago, p)) { if (p < lo) lo = p; n++; }
  }
  if (n < lookbackSlots / 2) return false;
  jump = now - lo;
  return true;
}

// WMO / shipping-forecast style tendency wording for a 3 h change.
enum TendencyClass : uint8_t {
  TEND_UNKNOWN = 0,
  TEND_STEADY,         // < 0.1 hPa
  TEND_SLOWLY,         // 0.1 - 1.5
  TEND_NORMAL,         // 1.6 - 3.5
  TEND_QUICKLY,        // 3.6 - 6.0
  TEND_VERY_RAPIDLY,   // > 6.0
};

inline TendencyClass classify3h(const Trend &t) {
  if (!t.valid) return TEND_UNKNOWN;
  float a = t.change < 0 ? -t.change : t.change;
  if (a < 0.1f) return TEND_STEADY;
  if (a < 1.55f) return TEND_SLOWLY;
  if (a < 3.55f) return TEND_NORMAL;
  if (a <= 6.0f) return TEND_QUICKLY;
  return TEND_VERY_RAPIDLY;
}

}  // namespace baro
