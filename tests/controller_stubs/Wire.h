#pragma once
#include <stdint.h>
struct TestWire {
  uint16_t pressed = 0;
  int index = 0;
  bool connected = true;
  void beginTransmission(unsigned) {}
  void write(unsigned) {}
  unsigned endTransmission() { return connected ? 0 : 1; }
  unsigned requestFrom(int, int) { index = 0; return 6; }
  int available() { return index < 6; }
  int read() { int i = index++; return i == 4 ? ((~pressed >> 8) & 255) : i == 5 ? (~pressed & 255) : 255; }
};
extern TestWire Wire;
