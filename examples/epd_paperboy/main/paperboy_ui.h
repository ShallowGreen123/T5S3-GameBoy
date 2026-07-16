#pragma once

#include <stdint.h>

#include "gbemu.h"
#include "touch_gt911.h"

enum {
  PAPERBOY_ACTION_POWER = 1U << 0,
  PAPERBOY_ACTION_SAVE = 1U << 1,
  PAPERBOY_ACTION_LOAD = 1U << 2,
};

static constexpr uint16_t PAPERBOY_LOGICAL_WIDTH = 540;
static constexpr uint16_t PAPERBOY_LOGICAL_HEIGHT = 960;
static constexpr uint16_t PAPERBOY_LOGICAL_PITCH = (PAPERBOY_LOGICAL_WIDTH + 7U) / 8U;
static constexpr uint16_t PAPERBOY_GAME_X = 32;
static constexpr uint16_t PAPERBOY_GAME_Y = 88;

void paperboy_ui_init();
uint8_t paperboy_ui_map_buttons(const touch_state_t *touch);
uint32_t paperboy_ui_map_actions(const touch_state_t *touch);
void paperboy_ui_draw_static(uint8_t *framebuffer);
void paperboy_ui_draw_dynamic(
    uint8_t *framebuffer,
    uint8_t buttons,
    bool power_on,
    bool save_available,
    const char *status_text);
