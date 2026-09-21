#include "snes_mini_controller.h"

#include <Arduino.h>
#include <Wire.h>
#include <esp_log.h>

#include "gbemu.h"

namespace {

constexpr char kTag[] = "snes_mini";
constexpr uint8_t kAddress = 0x52U;
constexpr uint32_t kPollIntervalMs = 16U;
constexpr uint32_t kReconnectIntervalMs = 1000U;
constexpr uint32_t kConversionDelayMs = 2U;

// Albert Gonzalez's SNES Mini report: ~(byte[4]<<8 | byte[5]).
// Bits below are in that *pressed* (active-high) representation.
constexpr uint16_t kUp = 0x0001U;
constexpr uint16_t kRight = 0x8000U;
constexpr uint16_t kDown = 0x4000U;
constexpr uint16_t kLeft = 0x0002U;
constexpr uint16_t kA = 0x0010U;
constexpr uint16_t kB = 0x0040U;
constexpr uint16_t kX = 0x0008U;
constexpr uint16_t kY = 0x0020U;
constexpr uint16_t kL = 0x2000U;
constexpr uint16_t kR = 0x0200U;
constexpr uint32_t kTurboHalfPeriodMs = 50U;  // 10 presses per second.
constexpr uint16_t kStart = 0x0400U;
constexpr uint16_t kSelect = 0x1000U;

bool g_initialized = false;
bool g_connected = false;
uint8_t g_buttons = 0U;
uint8_t g_actions = 0U;
uint8_t g_navigation_buttons = 0U;
bool g_settings_chord_active = false;
bool g_rotate_chord_active = false;
uint16_t g_previous_pressed = 0U;
uint32_t g_turbo_a_started_ms = 0U;
uint32_t g_turbo_b_started_ms = 0U;
bool g_select_consumed = false;
uint32_t g_last_poll_ms = 0U;
uint32_t g_next_probe_ms = 0U;

bool write_register(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(kAddress);
  Wire.write(reg);
  Wire.write(value);
  return Wire.endTransmission() == 0U;
}

void disconnect(uint32_t now, const char *reason) {
  if (g_connected) {
    ESP_LOGW(kTag, "controller disconnected: %s", reason);
  }
  g_connected = false;
  g_initialized = false;
  g_buttons = 0U;  // Release all keys; never leave a button stuck after unplug.
  g_actions = 0U;
  g_navigation_buttons = 0U;
  g_settings_chord_active = false;
  g_rotate_chord_active = false;
  g_previous_pressed = 0U;
  g_select_consumed = false;
  g_next_probe_ms = now + kReconnectIntervalMs;
}

bool probe_and_initialize(uint32_t now) {
  Wire.beginTransmission(kAddress);
  if (Wire.endTransmission() != 0U) {
    disconnect(now, "address 0x52 did not ACK");
    return false;
  }

  // SNES Mini needs the unencrypted extension-controller handshake.
  // NES Mini also accepts it. Do not scan the bus or touch PCA9535 (0x20).
  if (!write_register(0xF0U, 0x55U)) {
    disconnect(now, "F0 initialization failed");
    return false;
  }
  delay(10);
  // Second handshake byte from the linked Arduino reference. Some clones
  // ignore it; a subsequent valid six-byte report is the real presence test.
  if (!write_register(0xFBU, 0x00U)) {
    ESP_LOGW(kTag, "FB initialization was not acknowledged; attempting report");
  }
  delay(10);
  g_initialized = true;
  g_last_poll_ms = now - kPollIntervalMs;
  return true;
}

uint8_t decode_buttons(const uint8_t data[6], uint32_t now) {
  const uint16_t pressed = static_cast<uint16_t>(
      (static_cast<uint16_t>(data[4] ^ 0xFFU) << 8U) |
      static_cast<uint16_t>(data[5] ^ 0xFFU));
  const uint16_t rising = pressed & ~g_previous_pressed;
  constexpr uint16_t settings_chord = kL | kR | kStart | kSelect;
  g_navigation_buttons = 0U;
  if ((pressed & settings_chord) == settings_chord && !g_settings_chord_active) {
    g_settings_chord_active = true;
    g_actions = SNES_ACTION_SETTINGS;
  }
  if (g_settings_chord_active) {
    // Consume the whole chord until all four keys are released, regardless
    // of release order. Never turn its tail into save/load or brightness.
    g_previous_pressed = pressed;
    g_select_consumed = true;
    if (!(pressed & settings_chord)) g_settings_chord_active = false;
    return 0U;
  }
  constexpr uint16_t rotate_chord = kL | kR | kRight;
  const bool rotate_held = (pressed & rotate_chord) == rotate_chord;
  if (rotate_held && (!g_rotate_chord_active || (rising & kRight))) {
    g_rotate_chord_active = true;
    g_actions = SNES_ACTION_ROTATE;
  }
  if (g_rotate_chord_active) {
    // Consume Right and the shoulders through release; tapping Right again
    // while holding both shoulders cycles once more, with no hold repeat.
    g_previous_pressed = pressed;
    if (!(pressed & rotate_chord)) g_rotate_chord_active = false;
    return 0U;
  }
  if (rising & kX) g_turbo_a_started_ms = now;
  if (rising & kY) g_turbo_b_started_ms = now;
  const bool turbo_a = (pressed & kX) &&
      (((now - g_turbo_a_started_ms) / kTurboHalfPeriodMs) % 2U == 0U);
  const bool turbo_b = (pressed & kY) &&
      (((now - g_turbo_b_started_ms) / kTurboHalfPeriodMs) % 2U == 0U);

  if (!(pressed & kSelect)) g_select_consumed = false;
  if ((pressed & (kStart | kSelect)) == (kStart | kSelect)) {
    // Reserve shoulders while the Settings chord is being assembled.
  } else if ((pressed & kSelect) && (pressed & (kL | kR))) {
    g_select_consumed = true;
    // Both shoulders together are a no-op. Require a fresh shoulder press.
    if ((pressed & (kL | kR)) == kL && (rising & kL))
      g_actions |= SNES_ACTION_DIM;
    if ((pressed & (kL | kR)) == kR && (rising & kR))
      g_actions |= SNES_ACTION_BRIGHTEN;
  } else if (!(pressed & kSelect)) {
    if ((pressed & (kL | kR)) == kL && (rising & kL))
      g_actions |= SNES_ACTION_LOAD;
    if ((pressed & (kL | kR)) == kR && (rising & kR))
      g_actions |= SNES_ACTION_SAVE;
  }
  g_previous_pressed = pressed;
  uint8_t input = 0U;
  if (pressed & kUp) input |= GBEMU_INPUT_UP;
  if (pressed & kDown) input |= GBEMU_INPUT_DOWN;
  if (pressed & kLeft) input |= GBEMU_INPUT_LEFT;
  if (pressed & kRight) input |= GBEMU_INPUT_RIGHT;
  g_navigation_buttons = input;
  if (pressed & kA) g_navigation_buttons |= GBEMU_INPUT_A;
  if (pressed & kB) g_navigation_buttons |= GBEMU_INPUT_B;
  if ((pressed & kA) || turbo_a) input |= GBEMU_INPUT_A;
  if ((pressed & kB) || turbo_b) input |= GBEMU_INPUT_B;
  if (pressed & kStart) input |= GBEMU_INPUT_START;
  if ((pressed & kSelect) && !g_select_consumed) input |= GBEMU_INPUT_SELECT;
  return input;
}

}  // namespace

uint8_t snes_mini_controller_buttons() {
  g_actions = 0U;  // Actions belong only to this poll, never its cached report.
  const uint32_t now = millis();
  if (!g_initialized) {
    if (static_cast<int32_t>(now - g_next_probe_ms) < 0 ||
        !probe_and_initialize(now)) {
      return 0U;
    }
  }

  if ((now - g_last_poll_ms) < kPollIntervalMs) {
    return g_buttons;
  }
  g_last_poll_ms = now;

  Wire.beginTransmission(kAddress);
  Wire.write(0x00U);  // Select the button-report register.
  if (Wire.endTransmission() != 0U) {
    disconnect(now, "report request failed");
    return 0U;
  }
  delay(kConversionDelayMs);

  uint8_t data[6] = {};
  const size_t count = Wire.requestFrom(static_cast<int>(kAddress), 6);
  if (count != sizeof(data)) {
    while (Wire.available()) (void)Wire.read();
    disconnect(now, "short report");
    return 0U;
  }
  for (uint8_t &byte : data) {
    if (!Wire.available()) {
      disconnect(now, "incomplete report");
      return 0U;
    }
    byte = static_cast<uint8_t>(Wire.read());
  }
  // In this six-byte format bit 0 of byte 4 is reserved and reads as 1.
  // A zero-filled bus failure must not turn into every button being pressed.
  if ((data[4] & 0x01U) == 0U) {
    disconnect(now, "invalid report flags");
    return 0U;
  }
  g_buttons = decode_buttons(data, now);
  if (!g_connected) {
    ESP_LOGI(kTag, "SNES/NES Mini controller connected at 0x%02X", kAddress);
    g_connected = true;
  }
  return g_buttons;
}

uint8_t snes_mini_controller_take_actions() {
  const uint8_t actions = g_actions;
  g_actions = 0U;
  return actions;
}

uint8_t snes_mini_controller_navigation_buttons() {
  return g_navigation_buttons;
}
