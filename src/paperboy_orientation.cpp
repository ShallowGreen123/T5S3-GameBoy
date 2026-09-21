#include "paperboy_orientation.h"
namespace { PaperboyOrientation orientation = PaperboyOrientation::Portrait; }
PaperboyOrientation paperboy_orientation() { return orientation; }
bool paperboy_is_landscape() { return orientation != PaperboyOrientation::Portrait; }
void paperboy_orientation_cycle() {
  orientation = static_cast<PaperboyOrientation>((static_cast<unsigned>(orientation) + 1U) % 3U);
}
void paperboy_landscape_touch(uint16_t raw_x, uint16_t raw_y, uint16_t &x, uint16_t &y) {
  // Same portrait -> electrical-panel transform as main.cpp.
  x = raw_y; y = 539U - raw_x;
  if (orientation == PaperboyOrientation::LandscapeReverse) {
    x = 959U - x; y = 539U - y;
  }
}
