#pragma once

#include <stdint.h>

#include "gbemu.h"
#include "battery_power.h"
#include "touch_gt911.h"
#include "snes_mini_controller.h"

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
  PAPERBOY_ACTION_ROM_PREVIOUS = 1U << 10,
  PAPERBOY_ACTION_ROM_NEXT = 1U << 11,
  PAPERBOY_ACTION_ROM_LAUNCH = 1U << 12,
  PAPERBOY_ACTION_LOAD_LAST = 1U << 13,
  PAPERBOY_ACTION_AUDIO_ENGINE = 1U << 14,
  PAPERBOY_ACTION_SD_RESCAN = 1U << 15,
  PAPERBOY_ACTION_ROTATE = 1U << 19,
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
static constexpr uint8_t PAPERBOY_ROM_ROWS_VISIBLE = 6;

struct PaperboyRomLibraryView {
  bool mounted;
  uint16_t rom_count;
  uint16_t selection;
  uint16_t first_visible;
  const char *visible_names[PAPERBOY_ROM_ROWS_VISIBLE];
  const char *status;
  const char *audio_engine;
  bool audio_output_available;
  bool has_last_snapshot;
  uint32_t card_size_mb;
};

void paperboy_ui_init();
void paperboy_ui_on_page_changed();
uint8_t paperboy_ui_map_buttons(const touch_state_t *touch);
uint32_t paperboy_ui_map_actions(const touch_state_t *touch, PaperboyPage page);
void paperboy_ui_draw_static(
    uint8_t *framebuffer,
    const char *firmware_version);
void paperboy_ui_draw_dynamic(
    uint8_t *framebuffer,
    uint8_t buttons,
    bool power_on,
    bool save_available,
    const PaperboyBatteryStatus *battery,
    const char *notice);
void paperboy_ui_draw_page(
    uint8_t *framebuffer,
    PaperboyPage page,
    const PaperboyBatteryStatus *battery,
    const char *firmware_version,
    const char *rom_title,
    bool touch_available,
    const PaperboyRomLibraryView *rom_library);

// Controller navigation uses physical keys, independently of touch and turbo.
uint32_t paperboy_ui_map_controller(uint8_t buttons, PaperboyPage page, uint32_t now);
void paperboy_ui_controller_page_changed();
bool paperboy_ui_controller_ready();
uint8_t paperboy_ui_controller_selection();
