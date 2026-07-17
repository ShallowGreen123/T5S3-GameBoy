#pragma once

#include <stdint.h>

#include "gbemu.h"
#include "battery_power.h"
#include "touch_gt911.h"

enum {
  PAPERBOY_ACTION_POWER = 1U << 0,
  PAPERBOY_ACTION_SAVE = 1U << 1,
  PAPERBOY_ACTION_LOAD = 1U << 2,
  PAPERBOY_ACTION_SETTINGS = 1U << 3,
  PAPERBOY_ACTION_BACK = 1U << 4,
  PAPERBOY_ACTION_HOME = 1U << 5,
  PAPERBOY_ACTION_BATTERY = 1U << 6,
  PAPERBOY_ACTION_SD_CARD = 1U << 7,
  PAPERBOY_ACTION_ABOUT = 1U << 8,
  PAPERBOY_ACTION_REFRESH = 1U << 9,
};

enum class PaperboyPage : uint8_t {
  Game,
  Settings,
  Battery,
  SdCard,
  About,
};

static constexpr uint16_t PAPERBOY_LOGICAL_WIDTH = 540;
static constexpr uint16_t PAPERBOY_LOGICAL_HEIGHT = 960;
static constexpr uint16_t PAPERBOY_LOGICAL_PITCH = (PAPERBOY_LOGICAL_WIDTH + 7U) / 8U;
static constexpr uint16_t PAPERBOY_GAME_X = 32;
static constexpr uint16_t PAPERBOY_GAME_Y = 88;

void paperboy_ui_init();
void paperboy_ui_on_page_changed();
uint8_t paperboy_ui_map_buttons(const touch_state_t *touch);
uint32_t paperboy_ui_map_actions(const touch_state_t *touch, PaperboyPage page);
void paperboy_ui_draw_static(uint8_t *framebuffer);
void paperboy_ui_draw_dynamic(
    uint8_t *framebuffer,
    uint8_t buttons,
    bool power_on,
    bool save_available);
void paperboy_ui_draw_page(
    uint8_t *framebuffer,
    PaperboyPage page,
    const BatteryStatus *battery,
    const char *firmware_version,
    const char *rom_title,
    bool touch_available);
