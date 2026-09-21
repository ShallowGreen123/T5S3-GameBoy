#include "paperboy_ui.h"
#include "paperboy_landscape.h"

#include <Arduino.h>
#include <stdlib.h>
#include <string.h>

#include "mono_canvas.h"
#include "night_light.h"

namespace {

constexpr uint16_t kWidth = PAPERBOY_LOGICAL_WIDTH;
constexpr uint16_t kHeight = PAPERBOY_LOGICAL_HEIGHT;
constexpr uint16_t kPitch = PAPERBOY_LOGICAL_PITCH;
constexpr uint32_t kActionDebounceMs = 120U;
constexpr uint32_t kRomNavigationRepeatDelayMs = 500U;
constexpr uint32_t kRomNavigationRepeatRateMs = 150U;
constexpr uint32_t kRomNavigationActions =
    PAPERBOY_ACTION_ROM_PREVIOUS | PAPERBOY_ACTION_ROM_NEXT;
// Local actions are handled by the UI. Battery-page light actions also
// synthesize REFRESH so the existing console loop redraws the new level.
constexpr uint32_t kLightOffAction = 1UL << 16;
constexpr uint32_t kLightDownAction = 1UL << 17;
constexpr uint32_t kLightUpAction = 1UL << 18;
constexpr uint32_t kLightActions =
    kLightOffAction | kLightDownAction | kLightUpAction;
constexpr uint8_t kLightStepPercent = 1U;
constexpr uint8_t kLightMaxPercent = 10U;

struct Rect {
  int x;
  int y;
  int width;
  int height;
};

constexpr Rect kPowerRect = {20, 20, 280, 42};
constexpr Rect kSaveRect = {316, 20, 94, 42};
constexpr Rect kLoadRect = {420, 20, 100, 42};
constexpr Rect kSelectRect = {160, 842, 92, 30};
constexpr Rect kStartRect = {288, 842, 92, 30};
constexpr Rect kSettingsButtonRect = {390, 902, 130, 42};
constexpr Rect kMainBatteryRect = {20, 902, 130, 42};
// Use a much larger hit target than the visible frame. The old 140x42 target
// was too close to the lower edge for reliable finger taps on the GT911.
constexpr Rect kSettingsTouchRect = {320, 876, 220, 80};
// These occupy the right side of the existing black brand bar. They are
// outside the Game Boy playfield and do not inject A/B/D-pad input.
constexpr Rect kGameLightDownRect = {328, 534, 76, 40};
constexpr Rect kGameLightUpRect = {432, 534, 76, 40};

constexpr Rect kBackRect = {20, 20, 112, 44};
constexpr Rect kHomeRect = {408, 20, 112, 44};
constexpr Rect kBatteryRect = {30, 150, 480, 120};
constexpr Rect kSdCardRect = {30, 290, 480, 120};
constexpr Rect kAboutRect = {30, 430, 480, 120};
constexpr Rect kRefreshRect = {170, 856, 200, 40};
constexpr Rect kLightOffRect = {30, 778, 146, 56};
constexpr Rect kLightDownRect = {196, 778, 146, 56};
constexpr Rect kLightUpRect = {362, 778, 146, 56};
constexpr Rect kAudioEngineRect = {20, 158, 500, 52};
constexpr Rect kRomPreviousRect = {20, 686, 156, 54};
constexpr Rect kRomNextRect = {192, 686, 156, 54};
constexpr Rect kRomLaunchRect = {364, 686, 156, 54};
constexpr Rect kLoadLastRect = {20, 762, 242, 54};
constexpr Rect kSdRescanRect = {278, 762, 242, 54};
constexpr int kRomListY = 224;
constexpr int kRomRowStep = 68;
constexpr int kRomRowHeight = 58;

constexpr int kDpadX = 142;
constexpr int kDpadY = 702;
constexpr int kDpadTouchRadius = 116;
constexpr int kDpadDeadZone = 22;
constexpr int kButtonAX = 438;
constexpr int kButtonAY = 646;
constexpr int kButtonBX = 354;
constexpr int kButtonBY = 720;
constexpr int kButtonRadius = 40;

uint32_t g_last_action_mask = 0;
uint32_t g_last_action_ms[20] = {0};
bool g_ignore_actions_until_release = false;
bool g_ignore_buttons_until_release = false;
uint32_t g_rom_navigation_repeat_action = 0;
uint32_t g_rom_navigation_repeat_next_ms = 0;

void reset_rom_navigation_repeat() {
  g_rom_navigation_repeat_action = 0U;
  g_rom_navigation_repeat_next_ms = 0U;
}

bool deadline_reached(uint32_t now, uint32_t deadline) {
  return static_cast<int32_t>(now - deadline) >= 0;
}

bool point_in_rect(uint16_t x, uint16_t y, const Rect &rect) {
  return x >= rect.x && x < (rect.x + rect.width) &&
         y >= rect.y && y < (rect.y + rect.height);
}

bool point_in_circle(uint16_t x, uint16_t y, int center_x, int center_y, int radius) {
  const int dx = static_cast<int>(x) - center_x;
  const int dy = static_cast<int>(y) - center_y;
  return (dx * dx) + (dy * dy) <= (radius * radius);
}

uint32_t current_action_mask(const touch_state_t *touch, PaperboyPage page) {
  if (page == PaperboyPage::Game && paperboy_is_landscape()) return paperboy_landscape_actions(touch);
  uint32_t mask = 0;
  if (touch == nullptr) {
    return mask;
  }

  if (page != PaperboyPage::Game && touch->home_pressed) {
    mask |= PAPERBOY_ACTION_HOME;
  }
  if (!touch->touched) {
    return mask;
  }

  for (uint8_t i = 0; i < touch->points; ++i) {
    const uint16_t x = touch->x[i];
    const uint16_t y = touch->y[i];
    if (page == PaperboyPage::Game) {
      if (point_in_rect(x, y, Rect{24, 534, 250, 40})) mask |= PAPERBOY_ACTION_ROTATE;
      if (point_in_rect(x, y, kPowerRect)) {
        mask |= PAPERBOY_ACTION_POWER;
      }
      if (point_in_rect(x, y, kSaveRect)) {
        mask |= PAPERBOY_ACTION_SAVE;
      }
      if (point_in_rect(x, y, kLoadRect)) {
        mask |= PAPERBOY_ACTION_LOAD;
      }
      if (point_in_rect(x, y, kSettingsTouchRect)) {
        mask |= PAPERBOY_ACTION_SETTINGS;
      }
      if (point_in_rect(x, y, kGameLightDownRect)) {
        mask |= kLightDownAction;
      }
      if (point_in_rect(x, y, kGameLightUpRect)) {
        mask |= kLightUpAction;
      }
      continue;
    }

    if (point_in_rect(x, y, kBackRect)) {
      mask |= PAPERBOY_ACTION_BACK;
    }
    if (point_in_rect(x, y, kHomeRect)) {
      mask |= PAPERBOY_ACTION_HOME;
    }
    if (page == PaperboyPage::Settings) {
      if (point_in_rect(x, y, kBatteryRect)) {
        mask |= PAPERBOY_ACTION_BATTERY;
      }
      if (point_in_rect(x, y, kSdCardRect)) {
        mask |= PAPERBOY_ACTION_SD_CARD;
      }
      if (point_in_rect(x, y, kAboutRect)) {
        mask |= PAPERBOY_ACTION_ABOUT;
      }
    } else if (page == PaperboyPage::Battery) {
      if (point_in_rect(x, y, kRefreshRect)) {
        mask |= PAPERBOY_ACTION_REFRESH;
      }
      if (point_in_rect(x, y, kLightOffRect)) {
        mask |= kLightOffAction;
      }
      if (point_in_rect(x, y, kLightDownRect)) {
        mask |= kLightDownAction;
      }
      if (point_in_rect(x, y, kLightUpRect)) {
        mask |= kLightUpAction;
      }
    } else if (page == PaperboyPage::SdCard) {
      if (point_in_rect(x, y, kAudioEngineRect)) {
        mask |= PAPERBOY_ACTION_AUDIO_ENGINE;
      }
      if (point_in_rect(x, y, kRomPreviousRect)) {
        mask |= PAPERBOY_ACTION_ROM_PREVIOUS;
      }
      if (point_in_rect(x, y, kRomNextRect)) {
        mask |= PAPERBOY_ACTION_ROM_NEXT;
      }
      if (point_in_rect(x, y, kRomLaunchRect)) {
        mask |= PAPERBOY_ACTION_ROM_LAUNCH;
      }
      if (point_in_rect(x, y, kLoadLastRect)) {
        mask |= PAPERBOY_ACTION_LOAD_LAST;
      }
      if (point_in_rect(x, y, kSdRescanRect)) {
        mask |= PAPERBOY_ACTION_SD_RESCAN;
      }
    }
  }
  return mask;
}

void draw_button_box(uint8_t *framebuffer, const Rect &rect, const char *label, bool active) {
  mono_fill_rect(
      framebuffer, kPitch, kWidth, kHeight,
      rect.x, rect.y, rect.width, rect.height, active ? false : true);
  mono_draw_frame(
      framebuffer, kPitch, kWidth, kHeight,
      rect.x, rect.y, rect.width, rect.height, 2, false);
  if (label[0] != '\0') {
    const int label_width = static_cast<int>(strlen(label)) * 12 - 2;
    mono_draw_text(
        framebuffer, kPitch, kWidth, kHeight,
        rect.x + ((rect.width - label_width) / 2), rect.y + 13, label, 2, active);
  }
}

void draw_round_button(uint8_t *framebuffer, int center_x, int center_y, bool active) {
  mono_fill_circle(
      framebuffer, kPitch, kWidth, kHeight,
      center_x, center_y, kButtonRadius, false);
  if (active) {
    mono_fill_circle(
        framebuffer, kPitch, kWidth, kHeight,
        center_x, center_y, kButtonRadius - 9, true);
  }
}

void draw_dpad(uint8_t *framebuffer, uint8_t buttons) {
  mono_fill_rect(framebuffer, kPitch, kWidth, kHeight, 108, 604, 68, 196, false);
  mono_fill_rect(framebuffer, kPitch, kWidth, kHeight, 44, 668, 196, 68, false);

  if ((buttons & GBEMU_INPUT_UP) != 0U) {
    mono_fill_rect(framebuffer, kPitch, kWidth, kHeight, 119, 616, 46, 52, true);
  }
  if ((buttons & GBEMU_INPUT_DOWN) != 0U) {
    mono_fill_rect(framebuffer, kPitch, kWidth, kHeight, 119, 736, 46, 52, true);
  }
  if ((buttons & GBEMU_INPUT_LEFT) != 0U) {
    mono_fill_rect(framebuffer, kPitch, kWidth, kHeight, 56, 679, 52, 46, true);
  }
  if ((buttons & GBEMU_INPUT_RIGHT) != 0U) {
    mono_fill_rect(framebuffer, kPitch, kWidth, kHeight, 176, 679, 52, 46, true);
  }

  const bool up_white = (buttons & GBEMU_INPUT_UP) == 0U;
  const bool down_white = (buttons & GBEMU_INPUT_DOWN) == 0U;
  const bool left_white = (buttons & GBEMU_INPUT_LEFT) == 0U;
  const bool right_white = (buttons & GBEMU_INPUT_RIGHT) == 0U;
  mono_draw_line(framebuffer, kPitch, kWidth, kHeight, 142, 618, 122, 644, up_white);
  mono_draw_line(framebuffer, kPitch, kWidth, kHeight, 142, 618, 162, 644, up_white);
  mono_draw_line(framebuffer, kPitch, kWidth, kHeight, 142, 786, 122, 760, down_white);
  mono_draw_line(framebuffer, kPitch, kWidth, kHeight, 142, 786, 162, 760, down_white);
  mono_draw_line(framebuffer, kPitch, kWidth, kHeight, 58, 702, 84, 682, left_white);
  mono_draw_line(framebuffer, kPitch, kWidth, kHeight, 58, 702, 84, 722, left_white);
  mono_draw_line(framebuffer, kPitch, kWidth, kHeight, 226, 702, 200, 682, right_white);
  mono_draw_line(framebuffer, kPitch, kWidth, kHeight, 226, 702, 200, 722, right_white);
}

void draw_centered_text(
    uint8_t *framebuffer, int y, const char *text, uint8_t scale, bool white = false) {
  if (text == nullptr) {
    return;
  }
  const int text_width = static_cast<int>(strlen(text)) * 6 * scale;
  mono_draw_text(
      framebuffer, kPitch, kWidth, kHeight,
      (static_cast<int>(kWidth) - text_width) / 2, y, text, scale, white);
}

void draw_settings_header(uint8_t *framebuffer, const char *title) {
  draw_button_box(framebuffer, kBackRect, "BACK", false);
  draw_button_box(framebuffer, kHomeRect, "HOME", false);
  draw_centered_text(framebuffer, 92, title, 3);
  mono_draw_line(framebuffer, kPitch, kWidth, kHeight, 20, 130, 520, 130, false);
}

void draw_version_label(
    uint8_t *framebuffer, int y, const char *firmware_version) {
  char version[48];
  snprintf(
      version, sizeof(version), "VERSION %s",
      firmware_version == nullptr ? "UNKNOWN" : firmware_version);
  draw_centered_text(framebuffer, y, version, 1);
}

void draw_version_footer(uint8_t *framebuffer, const char *firmware_version) {
  mono_draw_line(framebuffer, kPitch, kWidth, kHeight, 120, 908, 420, 908, false);
  draw_version_label(framebuffer, 924, firmware_version);
}

void draw_menu_item(
    uint8_t *framebuffer, const Rect &rect, const char *title, const char *subtitle) {
  mono_draw_frame(
      framebuffer, kPitch, kWidth, kHeight,
      rect.x, rect.y, rect.width, rect.height, 3, false);
  mono_draw_text(
      framebuffer, kPitch, kWidth, kHeight,
      rect.x + 24, rect.y + 24, title, 2, false);
  mono_draw_text(
      framebuffer, kPitch, kWidth, kHeight,
      rect.x + 24, rect.y + 70, subtitle, 1, false);
  mono_draw_text(
      framebuffer, kPitch, kWidth, kHeight,
      rect.x + rect.width - 42, rect.y + 45, ">", 3, false);
}

void draw_settings_menu(uint8_t *framebuffer) {
  draw_menu_item(framebuffer, kBatteryRect, "BATTERY + LIGHT", "POWER AND BRIGHTNESS CONTROLS");
  draw_menu_item(framebuffer, kSdCardRect, "SD CARD", "ROM LIBRARY AND SAVE FILES");
  draw_menu_item(framebuffer, kAboutRect, "ABOUT SYSTEM", "DEVICE AND SOFTWARE INFO");
  const Rect options[] = {kBatteryRect, kSdCardRect, kAboutRect};
  const Rect &focus = options[paperboy_ui_controller_selection()];
  mono_draw_frame(framebuffer, kPitch, kWidth, kHeight,
                  focus.x - 7, focus.y - 7, focus.width + 14, focus.height + 14, 3, false);
  draw_centered_text(framebuffer, 650, "UP/DOWN: SELECT   A: OPEN   B: BACK", 1);
}

const char *battery_state_text(const PaperboyBatteryStatus &battery) {
  if (!battery.gauge_found && !battery.charger_found) {
    return "BATTERY HARDWARE NOT FOUND";
  }
  if (!battery.gauge_read_ok && !battery.charger_read_ok) {
    return "BATTERY READ ERROR";
  }
  if (battery.fault_present) {
    return "CHARGER FAULT";
  }
  if (battery.charge_done) {
    return "FULL";
  }
  if (battery.charging) {
    return "CHARGING";
  }
  if (battery.usb_connected && !battery.charge_enabled) {
    return "CHARGE DISABLED";
  }
  if (battery.average_current_ma < -20) {
    return "DISCHARGING";
  }
  return battery.usb_connected ? "USB POWER" : "IDLE";
}

const char *main_battery_state_text(const PaperboyBatteryStatus *battery) {
  if (battery == nullptr ||
      (!battery->gauge_read_ok && !battery->charger_read_ok)) {
    return "--";
  }
  if (battery->fault_present) {
    return "FAULT";
  }
  if (battery->charge_done) {
    return "FULL";
  }
  if (battery->charging) {
    return "CHG";
  }
  if (battery->usb_connected && !battery->charge_enabled) {
    return "OFF";
  }
  return battery->usb_connected ? "USB" : "BAT";
}

void draw_main_battery_indicator(
    uint8_t *framebuffer, const PaperboyBatteryStatus *battery) {

  constexpr int kIconX = 30;
  constexpr int kIconY = 912;
  constexpr int kIconWidth = 46;
  constexpr int kIconHeight = 22;
  constexpr int kFillWidth = 38;
  mono_draw_frame(
      framebuffer, kPitch, kWidth, kHeight,
      kIconX, kIconY, kIconWidth, kIconHeight, 2, false);
  mono_fill_rect(
      framebuffer, kPitch, kWidth, kHeight,
      kIconX + kIconWidth, kIconY + 7, 5, 8, false);

  const bool soc_available = battery != nullptr && battery->gauge_read_ok;
  const uint16_t soc = !soc_available
      ? 0U
      : (battery->soc_percent > 100U ? 100U : battery->soc_percent);
  const int fill_width = static_cast<int>((kFillWidth * soc + 99U) / 100U);
  if (fill_width > 0) {
    mono_fill_rect(
        framebuffer, kPitch, kWidth, kHeight,
        kIconX + 4, kIconY + 4, fill_width, kIconHeight - 8, false);
  }

  mono_draw_text(
      framebuffer, kPitch, kWidth, kHeight,
      90, 919, main_battery_state_text(battery), 1, false);
}

void draw_value_row(uint8_t *framebuffer, int y, const char *label, const char *value) {
  mono_draw_text(framebuffer, kPitch, kWidth, kHeight, 48, y, label, 2, false);
  mono_draw_text(framebuffer, kPitch, kWidth, kHeight, 278, y, value, 2, false);
  mono_draw_line(framebuffer, kPitch, kWidth, kHeight, 44, y + 28, 496, y + 28, false);
}

void draw_light_controls(uint8_t *framebuffer) {
  const uint8_t level = night_light_brightness();
  char title[40];
  snprintf(title, sizeof(title), "NIGHT LIGHT %u%% (MAX 10%%)", static_cast<unsigned>(level));
  draw_centered_text(framebuffer, 736, title, 2);
  draw_button_box(framebuffer, kLightOffRect, "OFF", level == 0U);
  draw_button_box(framebuffer, kLightDownRect, "DIM -", false);
  draw_button_box(framebuffer, kLightUpRect, "BRIGHT +", false);
}

void draw_battery_page(
    uint8_t *framebuffer, const PaperboyBatteryStatus *battery) {
  // Light controls remain accessible even when the battery gauge is missing.
  if (battery == nullptr) {
    draw_centered_text(framebuffer, 300, "BATTERY DATA UNAVAILABLE", 2);
    draw_light_controls(framebuffer);
    draw_button_box(framebuffer, kRefreshRect, "REFRESH", false);
    return;
  }

  const uint16_t bounded_soc = battery->soc_percent > 100U ? 100U : battery->soc_percent;
  mono_draw_frame(framebuffer, kPitch, kWidth, kHeight, 145, 154, 250, 92, 4, false);
  mono_fill_rect(framebuffer, kPitch, kWidth, kHeight, 395, 180, 14, 40, false);
  if (battery->gauge_read_ok && bounded_soc > 0U) {
    mono_fill_rect(
        framebuffer, kPitch, kWidth, kHeight,
        155, 164, static_cast<int>((230U * bounded_soc) / 100U), 72, false);
  }
  char text[48];
  snprintf(text, sizeof(text), "%u%%  %s", bounded_soc, battery_state_text(*battery));
  draw_centered_text(framebuffer, 266, text, 2);

  char value[48];
  snprintf(value, sizeof(value), "%u mV", battery->voltage_mv);
  draw_value_row(framebuffer, 330, "VOLTAGE", value);
  snprintf(value, sizeof(value), "%d / %d mA", battery->current_ma, battery->average_current_ma);
  draw_value_row(framebuffer, 382, "NOW / AVG", value);
  snprintf(
      value, sizeof(value), "%u / %u mAh",
      battery->remaining_capacity_mah, battery->full_capacity_mah);
  draw_value_row(framebuffer, 434, "CAPACITY", value);
  char temperature[24];
  if (battery->temperature_dk == 0U) {
    snprintf(temperature, sizeof(temperature), "--");
  } else {
    const int deci_c = static_cast<int>(battery->temperature_dk) - 2731;
    snprintf(
        temperature, sizeof(temperature),
        "%d.%d C", deci_c / 10, abs(deci_c % 10));
  }
  snprintf(
      value, sizeof(value), "%u%% / %s", battery->health_percent, temperature);
  draw_value_row(framebuffer, 486, "HEALTH / TEMP", value);
  snprintf(
      value, sizeof(value), "%u mV / %u mA",
      battery->vbus_voltage_mv, battery->configured_input_limit_ma);
  draw_value_row(framebuffer, 538, "VBUS / INPUT", value);
  snprintf(
      value, sizeof(value), "%u / %u mA",
      battery->configured_charge_current_ma, battery->charger_adc_current_ma);
  draw_value_row(framebuffer, 590, "CHG SET / ADC", value);
  snprintf(
      value, sizeof(value), "%u / %u mV",
      battery->configured_charge_voltage_mv, battery->system_voltage_mv);
  draw_value_row(framebuffer, 642, "VREG / VSYS", value);
  char hardware[80];
  snprintf(
      hardware, sizeof(hardware), "BQ27220 %s | BQ25896 %s | FAULT %s",
      !battery->gauge_found ? "MISSING" : (battery->gauge_read_ok ? "OK" : "ERROR"),
      !battery->charger_found ? "MISSING" : (battery->charger_read_ok ? "OK" : "ERROR"),
      battery->fault_present ? "YES" : "NONE");
  draw_centered_text(framebuffer, 698, hardware, 1);
  draw_light_controls(framebuffer);
  draw_button_box(framebuffer, kRefreshRect, "REFRESH", false);
}

void draw_sd_card_page(
    uint8_t *framebuffer, const PaperboyRomLibraryView *library) {
  const bool mounted = library != nullptr && library->mounted;
  char line[96];

  if (mounted) {
    snprintf(
        line, sizeof(line), "%u ROMS  %lu MB CARD",
        static_cast<unsigned>(library->rom_count),
        static_cast<unsigned long>(library->card_size_mb));
  } else {
    snprintf(line, sizeof(line), "SD CARD NOT MOUNTED");
  }
  draw_centered_text(framebuffer, 138, line, 1);

  const char *engine =
      library == nullptr || library->audio_engine == nullptr
          ? "MUTE"
          : library->audio_engine;
  snprintf(
      line, sizeof(line), "SOUND %s%s",
      engine,
      library != nullptr && library->audio_output_available ? "  GPIO" : "  NO SPEAKER");
  draw_button_box(framebuffer, kAudioEngineRect, line, false);

  if (!mounted) {
    draw_centered_text(framebuffer, 330, "INSERT A FAT32 SD CARD", 2);
    draw_centered_text(framebuffer, 380, "ROM FILES MAY BE IN ONE SUBFOLDER", 1);
  } else if (library->rom_count == 0U) {
    draw_centered_text(framebuffer, 330, "NO GB OR GBC FILES FOUND", 2);
    draw_centered_text(framebuffer, 380, "ADD DMG COMPATIBLE ROM FILES", 1);
  } else {
    for (uint8_t row = 0; row < PAPERBOY_ROM_ROWS_VISIBLE; ++row) {
      const uint16_t rom_index = static_cast<uint16_t>(library->first_visible + row);
      const char *name = library->visible_names[row];
      if (rom_index >= library->rom_count || name == nullptr) {
        continue;
      }

      const Rect row_rect = {30, kRomListY + (row * kRomRowStep), 480, kRomRowHeight};
      const bool selected = rom_index == library->selection;
      mono_fill_rect(
          framebuffer, kPitch, kWidth, kHeight,
          row_rect.x, row_rect.y, row_rect.width, row_rect.height,
          !selected);
      mono_draw_frame(
          framebuffer, kPitch, kWidth, kHeight,
          row_rect.x, row_rect.y, row_rect.width, row_rect.height, 2,
          selected);

      snprintf(
          line, sizeof(line), "%02u %s",
          static_cast<unsigned>(rom_index + 1U), name);
      line[37] = '\0';
      mono_draw_text(
          framebuffer, kPitch, kWidth, kHeight,
          row_rect.x + 14, row_rect.y + 19, line, 2, selected);
    }
  }

  draw_button_box(framebuffer, kRomPreviousRect, "PREV", false);
  draw_button_box(framebuffer, kRomNextRect, "NEXT", false);
  draw_button_box(framebuffer, kRomLaunchRect, "PLAY", false);
  draw_button_box(framebuffer, kLoadLastRect, "LOAD LAST", false);
  draw_button_box(framebuffer, kSdRescanRect, "RESCAN", false);

  if (library != nullptr && library->status != nullptr && library->status[0] != '\0') {
    draw_centered_text(framebuffer, 838, library->status, 1);
  } else if (library != nullptr && library->has_last_snapshot) {
    draw_centered_text(framebuffer, 838, "LOAD LAST RESTORES THE SAVED SNAPSHOT", 1);
  } else {
    draw_centered_text(framebuffer, 838, "SAVE CREATES SAV AND STATE BESIDE THE ROM", 1);
  }
}

void draw_about_page(
    uint8_t *framebuffer,
    const char *firmware_version,
    const char *rom_title,
    bool touch_available) {
  char value[64];
  draw_value_row(framebuffer, 180, "DEVICE", "LILYGO T5S3 PRO");
  draw_value_row(framebuffer, 240, "MCU", "ESP32-S3");
  draw_value_row(framebuffer, 300, "DISPLAY", "4.7 IN 960x540 EPD");
  draw_value_row(framebuffer, 360, "EMULATOR", "CRANKBOY / DMG");
  snprintf(value, sizeof(value), "%u MB", static_cast<unsigned>(ESP.getFlashChipSize() / (1024U * 1024U)));
  draw_value_row(framebuffer, 420, "FLASH", value);
  snprintf(value, sizeof(value), "%u MB", static_cast<unsigned>(ESP.getPsramSize() / (1024U * 1024U)));
  draw_value_row(framebuffer, 480, "PSRAM", value);
  draw_value_row(framebuffer, 540, "TOUCH", touch_available ? "GT911 ONLINE" : "NOT FOUND");
  draw_value_row(framebuffer, 600, "SOFTWARE", firmware_version == nullptr ? "UNKNOWN" : firmware_version);
  draw_value_row(framebuffer, 660, "GAME", rom_title == nullptr ? "UNKNOWN" : rom_title);
  draw_centered_text(framebuffer, 770, "T5S3 GAMEBOY", 3);
  draw_centered_text(framebuffer, 820, "OPEN SOURCE DEMO SYSTEM", 1);
}

}  // namespace

void paperboy_ui_init() {
  night_light_init();
  g_last_action_mask = 0;
  memset(g_last_action_ms, 0, sizeof(g_last_action_ms));
  g_ignore_actions_until_release = false;
  reset_rom_navigation_repeat();
}

void paperboy_ui_on_page_changed() {
  paperboy_ui_controller_page_changed();
  g_ignore_actions_until_release = true;
  g_ignore_buttons_until_release = true;
  reset_rom_navigation_repeat();
}

uint8_t paperboy_ui_map_buttons(const touch_state_t *touch) {
  if (g_ignore_buttons_until_release) {
    if (touch && !touch->touched) g_ignore_buttons_until_release = false;
    return 0;
  }
  if (paperboy_is_landscape()) return paperboy_landscape_buttons(touch);
  uint8_t buttons = 0;
  if (touch == nullptr || !touch->touched) {
    return buttons;
  }

  for (uint8_t i = 0; i < touch->points; ++i) {
    const uint16_t x = touch->x[i];
    const uint16_t y = touch->y[i];
    const int dx = static_cast<int>(x) - kDpadX;
    const int dy = static_cast<int>(y) - kDpadY;

    if ((dx * dx) + (dy * dy) <= (kDpadTouchRadius * kDpadTouchRadius)) {
      if (dx < -kDpadDeadZone) {
        buttons |= GBEMU_INPUT_LEFT;
      } else if (dx > kDpadDeadZone) {
        buttons |= GBEMU_INPUT_RIGHT;
      }
      if (dy < -kDpadDeadZone) {
        buttons |= GBEMU_INPUT_UP;
      } else if (dy > kDpadDeadZone) {
        buttons |= GBEMU_INPUT_DOWN;
      }
    }

    if (point_in_circle(x, y, kButtonAX, kButtonAY, kButtonRadius + 12)) {
      buttons |= GBEMU_INPUT_A;
    }
    if (point_in_circle(x, y, kButtonBX, kButtonBY, kButtonRadius + 12)) {
      buttons |= GBEMU_INPUT_B;
    }
    if (point_in_rect(x, y, kSelectRect)) {
      buttons |= GBEMU_INPUT_SELECT;
    }
    if (point_in_rect(x, y, kStartRect)) {
      buttons |= GBEMU_INPUT_START;
    }
  }
  return buttons;
}

uint32_t paperboy_ui_map_actions(const touch_state_t *touch, PaperboyPage page) {
  static const uint32_t kActionBits[] = {
      PAPERBOY_ACTION_POWER,
      PAPERBOY_ACTION_SAVE,
      PAPERBOY_ACTION_LOAD,
      PAPERBOY_ACTION_SETTINGS,
      PAPERBOY_ACTION_BACK,
      PAPERBOY_ACTION_HOME,
      PAPERBOY_ACTION_BATTERY,
      PAPERBOY_ACTION_SD_CARD,
      PAPERBOY_ACTION_ABOUT,
      PAPERBOY_ACTION_REFRESH,
      PAPERBOY_ACTION_ROM_PREVIOUS,
      PAPERBOY_ACTION_ROM_NEXT,
      PAPERBOY_ACTION_ROM_LAUNCH,
      PAPERBOY_ACTION_LOAD_LAST,
      PAPERBOY_ACTION_AUDIO_ENGINE,
      PAPERBOY_ACTION_SD_RESCAN,
      kLightOffAction,
      kLightDownAction,
      kLightUpAction,
      PAPERBOY_ACTION_ROTATE,
  };
  static_assert(sizeof(kActionBits) / sizeof(kActionBits[0]) ==
                    sizeof(g_last_action_ms) / sizeof(g_last_action_ms[0]),
                "action debounce array must include all light actions");
  const uint32_t now = millis();
  const uint32_t raw_current = current_action_mask(touch, page);
  const uint32_t raw_navigation = raw_current & kRomNavigationActions;
  const uint32_t held_navigation =
      (raw_navigation == PAPERBOY_ACTION_ROM_PREVIOUS ||
       raw_navigation == PAPERBOY_ACTION_ROM_NEXT)
          ? raw_navigation
          : 0U;
  const uint32_t current =
      (raw_current & ~kRomNavigationActions) | held_navigation;
  uint32_t fired = 0;

  if (g_ignore_actions_until_release) {
    reset_rom_navigation_repeat();
    g_last_action_mask = current;
    if (!touch || !touch->touched) {
      g_ignore_actions_until_release = false;
      g_last_action_mask = 0U;
    }
    return 0U;
  }

  for (uint8_t i = 0; i < (sizeof(kActionBits) / sizeof(kActionBits[0])); ++i) {
    const uint32_t bit = kActionBits[i];
    if ((current & bit) != 0U && (g_last_action_mask & bit) == 0U &&
        (now - g_last_action_ms[i]) >= kActionDebounceMs) {
      fired |= bit;
      g_last_action_ms[i] = now;
    }
  }

  if (held_navigation != PAPERBOY_ACTION_ROM_PREVIOUS &&
      held_navigation != PAPERBOY_ACTION_ROM_NEXT) {
    reset_rom_navigation_repeat();
  } else if (g_rom_navigation_repeat_action != held_navigation) {
    g_rom_navigation_repeat_action = held_navigation;
    g_rom_navigation_repeat_next_ms = now + kRomNavigationRepeatDelayMs;
  } else if (deadline_reached(now, g_rom_navigation_repeat_next_ms)) {
    fired |= held_navigation;
    g_rom_navigation_repeat_next_ms = now + kRomNavigationRepeatRateMs;
  }

  g_last_action_mask = current;
  const uint32_t light_action = fired & kLightActions;
  if (light_action != 0U) {
    const uint8_t previous = night_light_brightness();
    uint8_t next = previous;
    if ((light_action & kLightOffAction) != 0U) {
      next = 0U;
    } else if ((light_action & kLightUpAction) != 0U) {
      next = previous >= (kLightMaxPercent - kLightStepPercent)
          ? kLightMaxPercent : static_cast<uint8_t>(previous + kLightStepPercent);
    } else if ((light_action & kLightDownAction) != 0U) {
      next = previous < kLightStepPercent
          ? 0U : static_cast<uint8_t>(previous - kLightStepPercent);
    }
    (void)night_light_set_brightness(next);
    // The existing Battery-page REFRESH handler invalidates the scene.
    if (page == PaperboyPage::Battery) {
      fired |= PAPERBOY_ACTION_REFRESH;
    }
  }
  return fired & ~kLightActions;
}

void paperboy_ui_draw_static(
    uint8_t *framebuffer,
    const char *firmware_version) {
  if (framebuffer == nullptr) {
    return;
  }

  mono_clear(framebuffer, static_cast<size_t>(kPitch) * kHeight, true);
  mono_draw_frame(framebuffer, kPitch, kWidth, kHeight, 4, 4, 532, 952, 3, false);
  mono_draw_line(framebuffer, kPitch, kWidth, kHeight, 16, 72, 524, 72, false);
  mono_draw_frame(framebuffer, kPitch, kWidth, kHeight, 24, 80, 496, 448, 4, false);
  mono_fill_rect(framebuffer, kPitch, kWidth, kHeight, 24, 536, 496, 34, false);
  mono_draw_text(framebuffer, kPitch, kWidth, kHeight, 36, 546, "ROTATE SCREEN", 2, true);
  mono_draw_text(framebuffer, kPitch, kWidth, kHeight, 286, 547, "LIGHT", 1, true);
  mono_draw_text(framebuffer, kPitch, kWidth, kHeight, 361, 546, "-", 2, true);
  mono_draw_text(framebuffer, kPitch, kWidth, kHeight, 466, 546, "+", 2, true);

  mono_draw_text(framebuffer, kPitch, kWidth, kHeight, 344, 770, "B", 2, false);
  mono_draw_text(framebuffer, kPitch, kWidth, kHeight, 428, 696, "A", 2, false);
  mono_draw_text(framebuffer, kPitch, kWidth, kHeight, 169, 882, "SELECT", 1, false);
  mono_draw_text(framebuffer, kPitch, kWidth, kHeight, 304, 882, "START", 1, false);
  draw_version_label(framebuffer, 935, firmware_version);
}

void paperboy_ui_draw_dynamic(
    uint8_t *framebuffer,
    uint8_t buttons,
    bool power_on,
    bool save_available,
    const PaperboyBatteryStatus *battery,
    const char *notice) {
  if (framebuffer == nullptr) {
    return;
  }

  draw_button_box(framebuffer, kPowerRect, "ON-OFF", power_on);
  draw_button_box(framebuffer, kSaveRect, "SAVE", false);
  draw_button_box(framebuffer, kLoadRect, "LOAD", false);
  if (save_available) {
    mono_fill_circle(framebuffer, kPitch, kWidth, kHeight, 508, 51, 3, false);
  }
  draw_dpad(framebuffer, buttons);
  draw_round_button(framebuffer, kButtonAX, kButtonAY, (buttons & GBEMU_INPUT_A) != 0U);
  draw_round_button(framebuffer, kButtonBX, kButtonBY, (buttons & GBEMU_INPUT_B) != 0U);
  draw_button_box(framebuffer, kSelectRect, "", (buttons & GBEMU_INPUT_SELECT) != 0U);
  draw_button_box(framebuffer, kStartRect, "", (buttons & GBEMU_INPUT_START) != 0U);
  draw_main_battery_indicator(framebuffer, battery);
  draw_button_box(framebuffer, kSettingsButtonRect, "SETTING", false);

  const bool low_battery =
      power_on && battery != nullptr && battery->low_battery;
  if (low_battery) {
    mono_fill_rect(
        framebuffer, kPitch, kWidth, kHeight,
        PAPERBOY_GAME_X, PAPERBOY_GAME_Y, GBEMU_FRAME_WIDTH, 38, false);
    mono_draw_text(
        framebuffer, kPitch, kWidth, kHeight,
        168, PAPERBOY_GAME_Y + 11, "!! LOW BATTERY !!", 2, true);
  } else if (notice != nullptr && notice[0] != '\0') {
    mono_draw_text(
        framebuffer, kPitch, kWidth, kHeight,
        250, 548, notice, 1, true);
  }
}

void paperboy_ui_draw_page(
    uint8_t *framebuffer,
    PaperboyPage page,
    const PaperboyBatteryStatus *battery,
    const char *firmware_version,
    const char *rom_title,
    bool touch_available,
    const PaperboyRomLibraryView *rom_library) {
  if (framebuffer == nullptr || page == PaperboyPage::Game) {
    return;
  }

  mono_clear(framebuffer, static_cast<size_t>(kPitch) * kHeight, true);
  mono_draw_frame(framebuffer, kPitch, kWidth, kHeight, 4, 4, 532, 952, 3, false);
  switch (page) {
    case PaperboyPage::Settings:
      draw_settings_header(framebuffer, "SETTINGS");
      draw_settings_menu(framebuffer);
      break;
    case PaperboyPage::Battery:
      draw_settings_header(framebuffer, "BATTERY + LIGHT");
      draw_battery_page(framebuffer, battery);
      break;
    case PaperboyPage::SdCard:
      draw_settings_header(framebuffer, "SD CARD");
      draw_sd_card_page(framebuffer, rom_library);
      break;
    case PaperboyPage::About:
      draw_settings_header(framebuffer, "ABOUT SYSTEM");
      draw_about_page(framebuffer, firmware_version, rom_title, touch_available);
      break;
    case PaperboyPage::Game:
    default:
      return;
  }
  draw_version_footer(framebuffer, firmware_version);
}
