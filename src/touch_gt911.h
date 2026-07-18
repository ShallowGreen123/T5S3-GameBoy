#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef TOUCH_SWAP_XY
#define TOUCH_SWAP_XY 0
#endif

#ifndef TOUCH_INVERT_X
#define TOUCH_INVERT_X 0
#endif

#ifndef TOUCH_INVERT_Y
#define TOUCH_INVERT_Y 0
#endif

#ifndef TOUCH_CALIBRATION_MODE
#define TOUCH_CALIBRATION_MODE 0
#endif

typedef struct touch_state_s {
  bool touched;
  bool home_pressed;
  uint8_t points;
  uint16_t x[5];
  uint16_t y[5];
  uint8_t id[5];
} touch_state_t;

bool touch_init(void);
bool touch_read(touch_state_t *out_state);
void touch_set_rotation(int rotation);
void touch_debug_dump_once_per_second(void);

#ifdef __cplusplus
}
#endif
