#pragma once

#include <stdint.h>

#include "gbemu.h"
#include "touch_gt911.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
  GB_BTN_RIGHT = GBEMU_INPUT_RIGHT,
  GB_BTN_LEFT = GBEMU_INPUT_LEFT,
  GB_BTN_UP = GBEMU_INPUT_UP,
  GB_BTN_DOWN = GBEMU_INPUT_DOWN,
  GB_BTN_A = GBEMU_INPUT_A,
  GB_BTN_B = GBEMU_INPUT_B,
  GB_BTN_SELECT = GBEMU_INPUT_SELECT,
  GB_BTN_START = GBEMU_INPUT_START,
};

enum {
  SYS_ACTION_CLEAR = 1U << 0,
  SYS_ACTION_PAUSE = 1U << 1,
  SYS_ACTION_SAVE = 1U << 2,
  SYS_ACTION_LOAD = 1U << 3,
  SYS_ACTION_RESET_OPTIONAL = 1U << 4,
};

void vc_init(void);
uint8_t vc_map_gb_buttons(const touch_state_t *touch);
uint32_t vc_map_system_actions(const touch_state_t *touch);
void vc_draw_static_overlay(uint8_t *framebuffer);

#ifdef __cplusplus
}
#endif
