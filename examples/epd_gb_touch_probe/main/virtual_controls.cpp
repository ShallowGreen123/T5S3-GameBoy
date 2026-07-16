#include "virtual_controls.h"

#include <Arduino.h>

#include "mono_canvas.h"
#include "t5s3_epd_pins.h"

namespace {

constexpr uint16_t kScreenPitchBytes = t5s3_epd::kActiveWidth / 8U;
constexpr uint32_t kSystemActionDebounceMs = 300U;

struct Rect {
  int x;
  int y;
  int width;
  int height;
};

constexpr Rect kRectClear = {20, 18, 86, 40};
constexpr Rect kRectPause = {116, 18, 86, 40};
constexpr Rect kRectSave = {212, 18, 86, 40};
constexpr Rect kRectLoad = {308, 18, 86, 40};
constexpr Rect kRectReset = {20, 68, 120, 36};

constexpr Rect kRectUp = {112, 150, 92, 72};
constexpr Rect kRectDown = {112, 312, 92, 72};
constexpr Rect kRectLeft = {28, 231, 92, 72};
constexpr Rect kRectRight = {196, 231, 92, 72};
constexpr Rect kRectA = {282, 240, 112, 92};
constexpr Rect kRectB = {238, 344, 112, 92};
constexpr Rect kRectSelect = {92, 460, 128, 42};
constexpr Rect kRectStart = {250, 460, 128, 42};

uint32_t g_last_system_press_mask = 0;
uint32_t g_last_system_fire_ms[5] = {0, 0, 0, 0, 0};

bool point_in_rect(uint16_t x, uint16_t y, const Rect &rect) {
  return x >= rect.x && x < (rect.x + rect.width) && y >= rect.y && y < (rect.y + rect.height);
}

uint32_t map_current_system_press_mask(const touch_state_t *touch) {
  uint32_t mask = 0;
  if (touch == nullptr || !touch->touched) {
    return 0;
  }

  for (uint8_t i = 0; i < touch->points; ++i) {
    const uint16_t x = touch->x[i];
    const uint16_t y = touch->y[i];

    if (point_in_rect(x, y, kRectClear)) {
      mask |= SYS_ACTION_CLEAR;
    }
    if (point_in_rect(x, y, kRectPause)) {
      mask |= SYS_ACTION_PAUSE;
    }
    if (point_in_rect(x, y, kRectSave)) {
      mask |= SYS_ACTION_SAVE;
    }
    if (point_in_rect(x, y, kRectLoad)) {
      mask |= SYS_ACTION_LOAD;
    }
    if (point_in_rect(x, y, kRectReset)) {
      mask |= SYS_ACTION_RESET_OPTIONAL;
    }
  }
  return mask;
}

}  // namespace

void vc_init(void) {
  g_last_system_press_mask = 0;
  memset(g_last_system_fire_ms, 0, sizeof(g_last_system_fire_ms));
}

uint8_t vc_map_gb_buttons(const touch_state_t *touch) {
  uint8_t buttons = 0;
  if (touch == nullptr || !touch->touched) {
    return 0;
  }

  for (uint8_t i = 0; i < touch->points; ++i) {
    const uint16_t x = touch->x[i];
    const uint16_t y = touch->y[i];

    if (point_in_rect(x, y, kRectUp)) {
      buttons |= GB_BTN_UP;
    }
    if (point_in_rect(x, y, kRectDown)) {
      buttons |= GB_BTN_DOWN;
    }
    if (point_in_rect(x, y, kRectLeft)) {
      buttons |= GB_BTN_LEFT;
    }
    if (point_in_rect(x, y, kRectRight)) {
      buttons |= GB_BTN_RIGHT;
    }
    if (point_in_rect(x, y, kRectA)) {
      buttons |= GB_BTN_A;
    }
    if (point_in_rect(x, y, kRectB)) {
      buttons |= GB_BTN_B;
    }
    if (point_in_rect(x, y, kRectSelect)) {
      buttons |= GB_BTN_SELECT;
    }
    if (point_in_rect(x, y, kRectStart)) {
      buttons |= GB_BTN_START;
    }
  }
  return buttons;
}

uint32_t vc_map_system_actions(const touch_state_t *touch) {
  const uint32_t now = millis();
  const uint32_t current_mask = map_current_system_press_mask(touch);
  uint32_t actions = 0;
  static const uint32_t kActionBits[5] = {
      SYS_ACTION_CLEAR,
      SYS_ACTION_PAUSE,
      SYS_ACTION_SAVE,
      SYS_ACTION_LOAD,
      SYS_ACTION_RESET_OPTIONAL,
  };

  for (uint8_t i = 0; i < 5U; ++i) {
    const uint32_t bit = kActionBits[i];
    const bool currently_pressed = (current_mask & bit) != 0U;
    const bool previously_pressed = (g_last_system_press_mask & bit) != 0U;
    if (currently_pressed && !previously_pressed && (now - g_last_system_fire_ms[i]) >= kSystemActionDebounceMs) {
      g_last_system_fire_ms[i] = now;
      actions |= bit;
    }
  }

  g_last_system_press_mask = current_mask;
  return actions;
}

void vc_draw_static_overlay(uint8_t *framebuffer) {
  if (framebuffer == nullptr) {
    return;
  }

  mono_draw_frame(framebuffer, kScreenPitchBytes, t5s3_epd::kActiveWidth, t5s3_epd::kActiveHeight, 0, 0, 960, 540, 3, false);
  mono_draw_frame(framebuffer, kScreenPitchBytes, t5s3_epd::kActiveWidth, t5s3_epd::kActiveHeight, 430, 28, 436, 484, 2, false);
  mono_draw_text(framebuffer, kScreenPitchBytes, t5s3_epd::kActiveWidth, t5s3_epd::kActiveHeight, 24, 120, "TOUCH GB", 2, false);

  mono_draw_frame(framebuffer, kScreenPitchBytes, t5s3_epd::kActiveWidth, t5s3_epd::kActiveHeight, kRectClear.x, kRectClear.y, kRectClear.width, kRectClear.height, 2, false);
  mono_draw_frame(framebuffer, kScreenPitchBytes, t5s3_epd::kActiveWidth, t5s3_epd::kActiveHeight, kRectPause.x, kRectPause.y, kRectPause.width, kRectPause.height, 2, false);
  mono_draw_frame(framebuffer, kScreenPitchBytes, t5s3_epd::kActiveWidth, t5s3_epd::kActiveHeight, kRectSave.x, kRectSave.y, kRectSave.width, kRectSave.height, 2, false);
  mono_draw_frame(framebuffer, kScreenPitchBytes, t5s3_epd::kActiveWidth, t5s3_epd::kActiveHeight, kRectLoad.x, kRectLoad.y, kRectLoad.width, kRectLoad.height, 2, false);
  mono_draw_frame(framebuffer, kScreenPitchBytes, t5s3_epd::kActiveWidth, t5s3_epd::kActiveHeight, kRectReset.x, kRectReset.y, kRectReset.width, kRectReset.height, 2, false);

  mono_draw_text(framebuffer, kScreenPitchBytes, t5s3_epd::kActiveWidth, t5s3_epd::kActiveHeight, 32, 30, "CLR", 1, false);
  mono_draw_text(framebuffer, kScreenPitchBytes, t5s3_epd::kActiveWidth, t5s3_epd::kActiveHeight, 127, 30, "PAUSE", 1, false);
  mono_draw_text(framebuffer, kScreenPitchBytes, t5s3_epd::kActiveWidth, t5s3_epd::kActiveHeight, 228, 30, "SAVE", 1, false);
  mono_draw_text(framebuffer, kScreenPitchBytes, t5s3_epd::kActiveWidth, t5s3_epd::kActiveHeight, 323, 30, "LOAD", 1, false);
  mono_draw_text(framebuffer, kScreenPitchBytes, t5s3_epd::kActiveWidth, t5s3_epd::kActiveHeight, 36, 80, "RESET", 1, false);

  mono_draw_frame(framebuffer, kScreenPitchBytes, t5s3_epd::kActiveWidth, t5s3_epd::kActiveHeight, kRectUp.x, kRectUp.y, kRectUp.width, kRectUp.height, 2, false);
  mono_draw_frame(framebuffer, kScreenPitchBytes, t5s3_epd::kActiveWidth, t5s3_epd::kActiveHeight, kRectDown.x, kRectDown.y, kRectDown.width, kRectDown.height, 2, false);
  mono_draw_frame(framebuffer, kScreenPitchBytes, t5s3_epd::kActiveWidth, t5s3_epd::kActiveHeight, kRectLeft.x, kRectLeft.y, kRectLeft.width, kRectLeft.height, 2, false);
  mono_draw_frame(framebuffer, kScreenPitchBytes, t5s3_epd::kActiveWidth, t5s3_epd::kActiveHeight, kRectRight.x, kRectRight.y, kRectRight.width, kRectRight.height, 2, false);
  mono_draw_line(framebuffer, kScreenPitchBytes, t5s3_epd::kActiveWidth, t5s3_epd::kActiveHeight, 158, 196, 130, 214, false);
  mono_draw_line(framebuffer, kScreenPitchBytes, t5s3_epd::kActiveWidth, t5s3_epd::kActiveHeight, 158, 196, 186, 214, false);
  mono_draw_line(framebuffer, kScreenPitchBytes, t5s3_epd::kActiveWidth, t5s3_epd::kActiveHeight, 158, 340, 130, 322, false);
  mono_draw_line(framebuffer, kScreenPitchBytes, t5s3_epd::kActiveWidth, t5s3_epd::kActiveHeight, 158, 340, 186, 322, false);
  mono_draw_line(framebuffer, kScreenPitchBytes, t5s3_epd::kActiveWidth, t5s3_epd::kActiveHeight, 74, 267, 102, 239, false);
  mono_draw_line(framebuffer, kScreenPitchBytes, t5s3_epd::kActiveWidth, t5s3_epd::kActiveHeight, 74, 267, 102, 295, false);
  mono_draw_line(framebuffer, kScreenPitchBytes, t5s3_epd::kActiveWidth, t5s3_epd::kActiveHeight, 242, 267, 214, 239, false);
  mono_draw_line(framebuffer, kScreenPitchBytes, t5s3_epd::kActiveWidth, t5s3_epd::kActiveHeight, 242, 267, 214, 295, false);
  mono_draw_text(framebuffer, kScreenPitchBytes, t5s3_epd::kActiveWidth, t5s3_epd::kActiveHeight, 149, 162, "UP", 1, false);
  mono_draw_text(framebuffer, kScreenPitchBytes, t5s3_epd::kActiveWidth, t5s3_epd::kActiveHeight, 134, 340, "DOWN", 1, false);
  mono_draw_text(framebuffer, kScreenPitchBytes, t5s3_epd::kActiveWidth, t5s3_epd::kActiveHeight, 46, 258, "LEFT", 1, false);
  mono_draw_text(framebuffer, kScreenPitchBytes, t5s3_epd::kActiveWidth, t5s3_epd::kActiveHeight, 209, 258, "RIGHT", 1, false);

  mono_draw_frame(framebuffer, kScreenPitchBytes, t5s3_epd::kActiveWidth, t5s3_epd::kActiveHeight, kRectA.x, kRectA.y, kRectA.width, kRectA.height, 2, false);
  mono_draw_frame(framebuffer, kScreenPitchBytes, t5s3_epd::kActiveWidth, t5s3_epd::kActiveHeight, kRectB.x, kRectB.y, kRectB.width, kRectB.height, 2, false);
  mono_draw_text(framebuffer, kScreenPitchBytes, t5s3_epd::kActiveWidth, t5s3_epd::kActiveHeight, 326, 276, "A", 3, false);
  mono_draw_text(framebuffer, kScreenPitchBytes, t5s3_epd::kActiveWidth, t5s3_epd::kActiveHeight, 282, 380, "B", 3, false);

  mono_draw_frame(framebuffer, kScreenPitchBytes, t5s3_epd::kActiveWidth, t5s3_epd::kActiveHeight, kRectSelect.x, kRectSelect.y, kRectSelect.width, kRectSelect.height, 2, false);
  mono_draw_frame(framebuffer, kScreenPitchBytes, t5s3_epd::kActiveWidth, t5s3_epd::kActiveHeight, kRectStart.x, kRectStart.y, kRectStart.width, kRectStart.height, 2, false);
  mono_draw_text(framebuffer, kScreenPitchBytes, t5s3_epd::kActiveWidth, t5s3_epd::kActiveHeight, 116, 474, "SELECT", 1, false);
  mono_draw_text(framebuffer, kScreenPitchBytes, t5s3_epd::kActiveWidth, t5s3_epd::kActiveHeight, 286, 474, "START", 1, false);
}
