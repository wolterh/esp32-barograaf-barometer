// baro_series.h - fixed-interval ring buffer of pressure samples.
//
// Hardware independent (no Arduino headers) so it can be unit tested on a PC.
// Samples are stored as uint16 in 0.01 hPa steps with an 800 hPa offset
// (800.01 .. 1455.35 hPa). The value 0 marks "no data" (gap).
#pragma once
#include <stdint.h>

namespace baro {

constexpr uint16_t NO_DATA = 0;
constexpr float P_OFFSET = 800.0f;

inline uint16_t encodeP(float hPa) {
  float v = (hPa - P_OFFSET) * 100.0f + 0.5f;
  if (v < 1.0f) v = 1.0f;
  if (v > 65535.0f) v = 65535.0f;
  return (uint16_t)v;
}
inline float decodeP(uint16_t raw) { return P_OFFSET + raw / 100.0f; }

template <int N>
struct Series {
  static constexpr int CAP = N;
  uint16_t data[N];
  uint16_t head;          // index of newest sample
  uint16_t count;         // number of slots in use (valid or gap)
  uint32_t intervalS;     // slot length in seconds
  uint32_t newestEpoch;   // slot start (epoch, s) of newest sample

  void clear(uint32_t interval) {
    for (int i = 0; i < N; i++) data[i] = NO_DATA;
    head = 0;
    count = 0;
    intervalS = interval;
    newestEpoch = 0;
  }

  uint32_t slotOf(uint32_t epoch) const { return epoch - (epoch % intervalS); }

  // Store a sample for the slot containing `epoch`. Missing slots in between
  // are filled with NO_DATA so that index <-> time stays exact.
  // A sample for the current newest slot overwrites it.
  // A clock jump backwards (more than one slot) clears the series.
  void push(uint32_t epoch, float hPa) {
    uint32_t slot = slotOf(epoch);
    if (count == 0) {
      head = 0;
      count = 1;
      data[0] = encodeP(hPa);
      newestEpoch = slot;
      return;
    }
    if (slot < newestEpoch) {
      clear(intervalS);
      push(epoch, hPa);
      return;
    }
    uint32_t steps = (slot - newestEpoch) / intervalS;
    if (steps == 0) {
      data[head] = encodeP(hPa);
      return;
    }
    if (steps > (uint32_t)N) {  // gap longer than the whole buffer
      uint32_t interval = intervalS;
      clear(interval);
      push(epoch, hPa);
      return;
    }
    for (uint32_t s = 1; s < steps; s++) advance(NO_DATA);
    advance(encodeP(hPa));
    newestEpoch = slot;
  }

  // ago = 0 is the newest slot. Returns false for gaps / out of range.
  bool get(int ago, float &hPa) const {
    if (ago < 0 || ago >= count) return false;
    int idx = head - ago;
    if (idx < 0) idx += N;
    if (data[idx] == NO_DATA) return false;
    hPa = decodeP(data[idx]);
    return true;
  }

  // Age of newest sample relative to `now` in slots (0 = current slot).
  int32_t staleSlots(uint32_t now) const {
    if (count == 0) return 1 << 30;
    uint32_t slot = slotOf(now);
    if (slot < newestEpoch) return 0;
    return (int32_t)((slot - newestEpoch) / intervalS);
  }

 private:
  void advance(uint16_t raw) {
    head = (uint16_t)((head + 1) % N);
    data[head] = raw;
    if (count < N) count++;
  }
};

}  // namespace baro
