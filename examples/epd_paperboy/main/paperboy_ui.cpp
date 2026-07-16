#include "paperboy_ui.h"

#include <Arduino.h>
#include <stdlib.h>
#include <string.h>

#include "mono_canvas.h"

namespace {

constexpr uint16_t kWidth = PAPERBOY_LOGICAL_WIDTH;
constexpr uint16_t kHeight = PAPERBOY_LOGICAL_HEIGHT;
constexpr uint16_t kPitch = PAPERBOY_LOGICAL_PITCH;
constexpr uint32_t kActionDebounceMs = 300U;

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
uint32_t g_last_action_ms[3] = {0, 0, 0};

bool point_in_rect(uint16_t x, uint16_t y, const Rect &rect) {
  return x >= rect.x && x < (rect.x + rect.width) &&
         y >= rect.y && y < (rect.y + rect.height);
}

bool point_in_circle(uint16_t x, uint16_t y, int center_x, int center_y, int radius) {
  const int dx = static_cast<int>(x) - center_x;
  const int dy = static_cast<int>(y) - center_y;
  return (dx * dx) + (dy * dy) <= (radius * radius);
}

uint32_t current_action_mask(const touch_state_t *touch) {
  uint32_t mask = 0;
  if (touch == nullptr || !touch->touched) {
    return mask;
  }

  for (uint8_t i = 0; i < touch->points; ++i) {
    if (point_in_rect(touch->x[i], touch->y[i], kPowerRect)) {
      mask |= PAPERBOY_ACTION_POWER;
    }
    if (point_in_rect(touch->x[i], touch->y[i], kSaveRect)) {
      mask |= PAPERBOY_ACTION_SAVE;
    }
    if (point_in_rect(touch->x[i], touch->y[i], kLoadRect)) {
      mask |= PAPERBOY_ACTION_LOAD;
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

}  // namespace

void paperboy_ui_init() {
  g_last_action_mask = 0;
  memset(g_last_action_ms, 0, sizeof(g_last_action_ms));
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

uint32_t paperboy_ui_map_actions(const touch_state_t *touch) {
  static const uint32_t kActionBits[3] = {
      PAPERBOY_ACTION_POWER,
      PAPERBOY_ACTION_SAVE,
      PAPERBOY_ACTION_LOAD,
  };
  const uint32_t now = millis();
  const uint32_t current = current_action_mask(touch);
  uint32_t fired = 0;

  for (uint8_t i = 0; i < 3U; ++i) {
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
  mono_draw_text(framebuffer, kPitch, kWidth, kHeight, 36, 546, "PAPER BOY S3", 2, true);

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
    const char *status_text) {
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

  mono_fill_rect(framebuffer, kPitch, kWidth, kHeight, 24, 910, 492, 32, true);
  if (status_text != nullptr && status_text[0] != '\0') {
    mono_draw_frame(framebuffer, kPitch, kWidth, kHeight, 24, 910, 492, 32, 2, false);
    mono_draw_text(framebuffer, kPitch, kWidth, kHeight, 38, 920, status_text, 1, false);
  }
}
