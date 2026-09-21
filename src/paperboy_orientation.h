#pragma once
#include <stdint.h>

// The H752-01 has no onboard accelerometer. Manual rotation is available
// without probing unrelated I2C peripherals or changing their registers.
enum class PaperboyOrientation : uint8_t { Portrait, Landscape, LandscapeReverse };
PaperboyOrientation paperboy_orientation();
void paperboy_orientation_cycle();
bool paperboy_is_landscape();
void paperboy_landscape_touch(uint16_t raw_x, uint16_t raw_y, uint16_t &x, uint16_t &y);
