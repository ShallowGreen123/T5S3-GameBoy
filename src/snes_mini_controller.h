#pragma once

#include <stdint.h>

// Optional SNES/NES Classic Mini controller on the *existing* board I2C bus:
// GPIO39 SDA, GPIO40 SCL, 3.3 V, common ground, slave address 0x52.
// This module never initializes/reconfigures Wire or touches other I2C slaves.
// Safe to call once per emulated frame. Returns zero when absent or on error;
// retries after disconnection and supports simultaneous touchscreen input.
uint8_t snes_mini_controller_buttons();

// Poll buttons first, then consume these one-shot actions on the same task.
enum SnesControllerAction : uint8_t {
  SNES_ACTION_SAVE = 1U << 0,
  SNES_ACTION_LOAD = 1U << 1,
  SNES_ACTION_DIM = 1U << 2,
  SNES_ACTION_BRIGHTEN = 1U << 3,
  SNES_ACTION_SETTINGS = 1U << 4,
  SNES_ACTION_ROTATE = 1U << 5,
};
uint8_t snes_mini_controller_take_actions();

// Physical D-pad/A/B only, without X/Y turbo; read after polling buttons.
uint8_t snes_mini_controller_navigation_buttons();
