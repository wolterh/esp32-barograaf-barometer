// Minimal Arduino stand-in so Adafruit_GFX + display.cpp build on a PC.
#pragma once
#include <math.h>
#include <string>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef ARDUINO
#define ARDUINO 10813
#endif
typedef bool boolean;
typedef uint8_t byte;
#define PROGMEM
#define pgm_read_byte(a) (*(const uint8_t *)(a))
#define pgm_read_word(a) (*(const uint16_t *)(a))
#define pgm_read_dword(a) (*(const uint32_t *)(a))
#define pgm_read_pointer(a) (*(void *const *)(a))
#ifndef min
#define min(a, b) ((a) < (b) ? (a) : (b))
#endif
#ifndef max
#define max(a, b) ((a) > (b) ? (a) : (b))
#endif
#ifndef _swap_int16_t
#define _swap_int16_t(a, b) { int16_t t = a; a = b; b = t; }
#endif
class __FlashStringHelper;
#define F(s) (s)
#define radians(d) ((d) * 0.017453292519943295)
class String : public std::string {
 public:
  String(const char *s = "") : std::string(s) {}
};
#include "Print.h"
