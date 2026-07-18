#include "paperboy_ui.h"

#include <Arduino.h>
#include <stdlib.h>
#include <string.h>

#include "mono_canvas.h"

namespace {

constexpr uint16_t kWidth = PAPERBOY_LOGICAL_WIDTH;
constexpr uint16_t kHeight = PAPERBOY_LOGICAL_HEIGHT;
constexpr uint16_t kPitch = PAPERBOY_LOGICAL_PITCH;
constexpr uint32_t kActionDebounceMs = 120U;

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

constexpr Rect kBackRect = {20, 20, 112, 44};
constexpr Rect kHomeRect = {408, 20, 112, 44};
constexpr Rect kBatteryRect = {30, 150, 480, 120};
constexpr Rect kSdCardRect = {30, 290, 480, 120};
constexpr Rect kAboutRect = {30, 430, 480, 120};
constexpr Rect kRefreshRect = {170, 826, 200, 48};

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
uint32_t g_last_action_ms[10] = {0};
bool g_ignore_actions_until_release = false;

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
    } else if (page == PaperboyPage::Battery && point_in_rect(x, y, kRefreshRect)) {
      mask |= PAPERBOY_ACTION_REFRESH;
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

void draw_version_footer(uint8_t *framebuffer, const char *firmware_version) {
  char version[48];
  snprintf(
      version, sizeof(version), "VERSION %s",
      firmware_version == nullptr ? "UNKNOWN" : firmware_version);
  mono_draw_line(framebuffer, kPitch, kWidth, kHeight, 120, 908, 420, 908, false);
  draw_centered_text(framebuffer, 924, version, 1);
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
  draw_menu_item(framebuffer, kBatteryRect, "BATTERY STATUS", "POWER AND CHARGE DETAILS");
  draw_menu_item(framebuffer, kSdCardRect, "SD CARD", "GAME LIBRARY (COMING SOON)");
  draw_menu_item(framebuffer, kAboutRect, "ABOUT SYSTEM", "DEVICE AND SOFTWARE INFO");
  draw_centered_text(framebuffer, 650, "SELECT AN ITEM TO OPEN", 1);
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
  mono_fill_rect(
      framebuffer, kPitch, kWidth, kHeight,
      kMainBatteryRect.x, kMainBatteryRect.y,
      kMainBatteryRect.width, kMainBatteryRect.height, true);
  mono_draw_frame(
      framebuffer, kPitch, kWidth, kHeight,
      kMainBatteryRect.x, kMainBatteryRect.y,
      kMainBatteryRect.width, kMainBatteryRect.height, 2, false);

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

  char percent[8];
  if (soc_available) {
    snprintf(percent, sizeof(percent), "%u%%", soc);
  } else {
    snprintf(percent, sizeof(percent), "--%%");
  }
  mono_draw_text(
      framebuffer, kPitch, kWidth, kHeight,
      92, 914, percent, 2, false);
  mono_draw_text(
      framebuffer, kPitch, kWidth, kHeight,
      120, 919, main_battery_state_text(battery), 1, false);
}

void draw_value_row(uint8_t *framebuffer, int y, const char *label, const char *value) {
  mono_draw_text(framebuffer, kPitch, kWidth, kHeight, 48, y, label, 2, false);
  mono_draw_text(framebuffer, kPitch, kWidth, kHeight, 278, y, value, 2, false);
  mono_draw_line(framebuffer, kPitch, kWidth, kHeight, 44, y + 28, 496, y + 28, false);
}

void draw_battery_page(
    uint8_t *framebuffer, const PaperboyBatteryStatus *battery) {
  if (battery == nullptr) {
    draw_centered_text(framebuffer, 300, "BATTERY DATA UNAVAILABLE", 2);
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
  snprintf(
      value, sizeof(value), "%u / %u mA",
      battery->configured_precharge_current_ma,
      battery->configured_termination_current_ma);
  draw_value_row(framebuffer, 694, "PRE / TERM", value);
  snprintf(
      value, sizeof(value), "CHG %s / HIZ %s",
      battery->charge_enabled ? "ON" : "OFF",
      battery->hiz_enabled ? "ON" : "OFF");
  draw_value_row(framebuffer, 746, "POWER PATH", value);

  char hardware[80];
  snprintf(
      hardware, sizeof(hardware), "BQ27220 %s | BQ25896 %s | FAULT %s",
      !battery->gauge_found ? "MISSING" : (battery->gauge_read_ok ? "OK" : "ERROR"),
      !battery->charger_found ? "MISSING" : (battery->charger_read_ok ? "OK" : "ERROR"),
      battery->fault_present ? "YES" : "NONE");
  draw_centered_text(framebuffer, 792, hardware, 1);
  draw_button_box(framebuffer, kRefreshRect, "REFRESH", false);
}

void draw_sd_card_page(uint8_t *framebuffer) {
  mono_draw_frame(framebuffer, kPitch, kWidth, kHeight, 165, 190, 210, 260, 5, false);
  mono_fill_rect(framebuffer, kPitch, kWidth, kHeight, 185, 190, 105, 42, false);
  mono_draw_text(framebuffer, kPitch, kWidth, kHeight, 218, 292, "SD", 5, false);
  draw_centered_text(framebuffer, 520, "SD CARD GAME LIBRARY", 2);
  draw_centered_text(framebuffer, 570, "UI PLACEHOLDER IS READY", 1);
  draw_centered_text(framebuffer, 610, "CARD ACCESS IS NOT IMPLEMENTED", 1);
  mono_draw_frame(framebuffer, kPitch, kWidth, kHeight, 120, 690, 300, 62, 3, false);
  draw_centered_text(framebuffer, 710, "COMING SOON", 2);
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
  draw_value_row(framebuffer, 360, "EMULATOR", "PEANUT-GB / DMG");
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
  g_last_action_mask = 0;
  memset(g_last_action_ms, 0, sizeof(g_last_action_ms));
  g_ignore_actions_until_release = false;
}

void paperboy_ui_on_page_changed() {
  g_ignore_actions_until_release = true;
}

uint8_t paperboy_ui_map_buttons(const touch_state_t *touch) {
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
  static const uint32_t kActionBits[10] = {
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
  };
  const uint32_t now = millis();
  const uint32_t current = current_action_mask(touch, page);
  uint32_t fired = 0;

  if (g_ignore_actions_until_release) {
    g_last_action_mask = current;
    if (current == 0U) {
      g_ignore_actions_until_release = false;
      g_last_action_mask = 0U;
    }
    return 0U;
  }

  for (uint8_t i = 0; i < 10U; ++i) {
    const uint32_t bit = kActionBits[i];
    if ((current & bit) != 0U && (g_last_action_mask & bit) == 0U &&
        (now - g_last_action_ms[i]) >= kActionDebounceMs) {
      fired |= bit;
      g_last_action_ms[i] = now;
    }
  }
  g_last_action_mask = current;
  return fired;
}

void paperboy_ui_draw_static(uint8_t *framebuffer) {
  if (framebuffer == nullptr) {
    return;
  }

  mono_clear(framebuffer, static_cast<size_t>(kPitch) * kHeight, true);
  mono_draw_frame(framebuffer, kPitch, kWidth, kHeight, 4, 4, 532, 952, 3, false);
  mono_draw_line(framebuffer, kPitch, kWidth, kHeight, 16, 72, 524, 72, false);
  mono_draw_frame(framebuffer, kPitch, kWidth, kHeight, 24, 80, 496, 448, 4, false);
  mono_fill_rect(framebuffer, kPitch, kWidth, kHeight, 24, 536, 496, 34, false);
  mono_draw_text(framebuffer, kPitch, kWidth, kHeight, 36, 546, "T5S3 GAMEBOY", 2, true);

  mono_draw_text(framebuffer, kPitch, kWidth, kHeight, 344, 770, "B", 2, false);
  mono_draw_text(framebuffer, kPitch, kWidth, kHeight, 428, 696, "A", 2, false);
  mono_draw_text(framebuffer, kPitch, kWidth, kHeight, 169, 882, "SELECT", 1, false);
  mono_draw_text(framebuffer, kPitch, kWidth, kHeight, 304, 882, "START", 1, false);
}

void paperboy_ui_draw_dynamic(
    uint8_t *framebuffer,
    uint8_t buttons,
    bool power_on,
    bool save_available,
    const PaperboyBatteryStatus *battery) {
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
}

void paperboy_ui_draw_page(
    uint8_t *framebuffer,
    PaperboyPage page,
    const PaperboyBatteryStatus *battery,
    const char *firmware_version,
    const char *rom_title,
    bool touch_available) {
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
      draw_settings_header(framebuffer, "BATTERY STATUS");
      draw_battery_page(framebuffer, battery);
      break;
    case PaperboyPage::SdCard:
      draw_settings_header(framebuffer, "SD CARD");
      draw_sd_card_page(framebuffer);
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
