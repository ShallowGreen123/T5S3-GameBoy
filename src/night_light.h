#pragma once

#include <stdint.h>

// T5S3 Pro GPIO11 LED boost enable. 0=off; 1..10 are actual hardware
// PWM percentages, with 10% the highest allowed brightness.
// State is saved to ESP32 NVS so the control also works without an SD card.
void night_light_init();
uint8_t night_light_brightness();
bool night_light_set_brightness(uint8_t percent);
void night_light_shutdown();
