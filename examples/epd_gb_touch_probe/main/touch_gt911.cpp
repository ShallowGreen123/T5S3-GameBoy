#include "touch_gt911.h"

#include <Arduino.h>
#include <Wire.h>
#include <esp_log.h>
#include <string.h>

#include "t5s3_epd_pins.h"

namespace {

constexpr const char *kTag = "touch_gt911";
constexpr uint8_t kPrimaryAddress = 0x5D;
constexpr uint8_t kFallbackAddress = 0x14;
constexpr uint16_t kRegProductId = 0x8140;
constexpr uint16_t kRegStatus = 0x814E;
constexpr uint16_t kRegFirstPoint = 0x814F;
constexpr uint8_t kReadyMask = 0x80;
constexpr uint8_t kTouchCountMask = 0x0F;
constexpr uint32_t kI2cTimeoutMs = 20;

bool g_touch_available = false;
uint8_t g_touch_address = 0;
int g_rotation = 0;
uint16_t g_raw_max_x = t5s3_epd::kPanelWidth;
uint16_t g_raw_max_y = t5s3_epd::kPanelHeight;
char g_product_id[5] = "----";
touch_state_t g_last_state = {};
touch_state_t g_last_raw_state = {};
uint32_t g_last_debug_dump_ms = 0;
uint32_t g_last_calibration_dump_ms = 0;

bool write_reg8(uint16_t reg, uint8_t value) {
  Wire.beginTransmission(g_touch_address);
  Wire.write((uint8_t)(reg >> 8));
  Wire.write((uint8_t)(reg & 0xFFU));
  Wire.write(value);
  return Wire.endTransmission() == 0;
}

bool read_regs(uint16_t reg, uint8_t *data, size_t size) {
  if (data == nullptr || size == 0U) {
    return false;
  }

  Wire.beginTransmission(g_touch_address);
  Wire.write((uint8_t)(reg >> 8));
  Wire.write((uint8_t)(reg & 0xFFU));
  if (Wire.endTransmission(false) != 0) {
    return false;
  }
  if (Wire.requestFrom((int)g_touch_address, (int)size) != (int)size) {
    return false;
  }
  for (size_t i = 0; i < size; ++i) {
    data[i] = (uint8_t)Wire.read();
  }
  return true;
}

void transform_point(uint16_t raw_x, uint16_t raw_y, uint16_t *out_x, uint16_t *out_y, bool *valid) {
  int32_t x = raw_x;
  int32_t y = raw_y;
  int32_t span_x = g_raw_max_x;
  int32_t span_y = g_raw_max_y;

#if TOUCH_SWAP_XY
  {
    const int32_t swap_value = x;
    x = y;
    y = swap_value;

    const int32_t swap_span = span_x;
    span_x = span_y;
    span_y = swap_span;
  }
#endif

#if TOUCH_INVERT_X
  if (span_x > 0) {
    x = span_x - 1 - x;
  }
#endif

#if TOUCH_INVERT_Y
  if (span_y > 0) {
    y = span_y - 1 - y;
  }
#endif

  switch (g_rotation & 3) {
    case 1: {
      const int32_t nx = span_y - 1 - y;
      y = x;
      x = nx;
      break;
    }
    case 2:
      x = span_x - 1 - x;
      y = span_y - 1 - y;
      break;
    case 3: {
      const int32_t ny = span_x - 1 - x;
      x = y;
      y = ny;
      break;
    }
    default:
      break;
  }

  *valid = x >= 0 && y >= 0 && x < t5s3_epd::kActiveWidth && y < t5s3_epd::kActiveHeight;
  if (*valid) {
    *out_x = (uint16_t)x;
    *out_y = (uint16_t)y;
  }
}

void log_calibration_samples() {
#if TOUCH_CALIBRATION_MODE
  const uint32_t now = millis();
  if (!g_last_raw_state.touched || (now - g_last_calibration_dump_ms) < 100U) {
    return;
  }

  g_last_calibration_dump_ms = now;
  ESP_LOGI(
      kTag,
      "cal raw id=%u x=%u y=%u mapped x=%u y=%u count=%u",
      (unsigned)g_last_raw_state.id[0],
      (unsigned)g_last_raw_state.x[0],
      (unsigned)g_last_raw_state.y[0],
      (unsigned)g_last_state.x[0],
      (unsigned)g_last_state.y[0],
      (unsigned)g_last_state.points);
#endif
}

}  // namespace

bool touch_init(void) {
  g_touch_available = false;
  g_touch_address = 0;
  g_rotation = 0;
  strcpy(g_product_id, "----");
  memset(&g_last_state, 0, sizeof(g_last_state));
  memset(&g_last_raw_state, 0, sizeof(g_last_raw_state));
  g_raw_max_x = t5s3_epd::kPanelWidth;
  g_raw_max_y = t5s3_epd::kPanelHeight;

  Wire.begin(t5s3_epd::kI2cSda, t5s3_epd::kI2cScl);
  Wire.setClock(400000);
  Wire.setTimeout(kI2cTimeoutMs);

  pinMode(t5s3_epd::kTouchRst, OUTPUT);
  digitalWrite(t5s3_epd::kTouchRst, HIGH);
  pinMode(t5s3_epd::kTouchInt, OUTPUT);
  digitalWrite(t5s3_epd::kTouchInt, HIGH);
  delay(5);

  for (uint8_t candidate : {kPrimaryAddress, kFallbackAddress}) {
    Wire.beginTransmission(candidate);
    if (Wire.endTransmission() == 0) {
      g_touch_address = candidate;
      break;
    }
  }

  if (g_touch_address == 0U) {
    ESP_LOGW(kTag, "GT911 probe failed at 0x%02X/0x%02X", kPrimaryAddress, kFallbackAddress);
    return false;
  }

  pinMode(t5s3_epd::kTouchInt, INPUT_PULLUP);

  uint8_t id_buf[11] = {0};
  if (!read_regs(kRegProductId, id_buf, sizeof(id_buf))) {
    ESP_LOGW(kTag, "GT911 found at 0x%02X but product ID read failed", g_touch_address);
    return false;
  }

  memcpy(g_product_id, id_buf, 4);
  g_product_id[4] = '\0';
  g_raw_max_x = (uint16_t)(id_buf[6] | ((uint16_t)id_buf[7] << 8));
  g_raw_max_y = (uint16_t)(id_buf[8] | ((uint16_t)id_buf[9] << 8));
  if (g_raw_max_x == 0U || g_raw_max_y == 0U) {
    g_raw_max_x = t5s3_epd::kPanelWidth;
    g_raw_max_y = t5s3_epd::kPanelHeight;
  }

  g_touch_available = true;
  ESP_LOGI(
      kTag,
      "GT911 found addr=0x%02X product=%s raw_max=%ux%u",
      g_touch_address,
      g_product_id,
      (unsigned)g_raw_max_x,
      (unsigned)g_raw_max_y);
  return true;
}

bool touch_read(touch_state_t *out_state) {
  uint8_t status = 0;
  uint8_t raw_points[1 + (5 * 8)] = {0};

  if (out_state == nullptr) {
    return false;
  }
  memset(out_state, 0, sizeof(*out_state));

  if (!g_touch_available) {
    return false;
  }
  memset(&g_last_raw_state, 0, sizeof(g_last_raw_state));
  if (!read_regs(kRegStatus, &status, 1)) {
    return false;
  }
  if ((status & kReadyMask) == 0U) {
    g_last_state = *out_state;
    g_last_raw_state = *out_state;
    return true;
  }

  const uint8_t point_count = (uint8_t)(status & kTouchCountMask);
  if (point_count == 0U) {
    (void)write_reg8(kRegStatus, 0);
    g_last_state = *out_state;
    g_last_raw_state = *out_state;
    return true;
  }
  if (point_count > 5U) {
    ESP_LOGW(kTag, "ignoring malformed GT911 packet count=%u", (unsigned)point_count);
    (void)write_reg8(kRegStatus, 0);
    return false;
  }

  if (!read_regs(kRegFirstPoint, raw_points, 1U + (size_t)point_count * 8U)) {
    (void)write_reg8(kRegStatus, 0);
    return false;
  }

  for (uint8_t i = 0; i < point_count; ++i) {
    const size_t base = (size_t)i * 8U;
    const uint8_t track_id = raw_points[base];
    const uint16_t raw_x = (uint16_t)(raw_points[base + 1U] | ((uint16_t)raw_points[base + 2U] << 8));
    const uint16_t raw_y = (uint16_t)(raw_points[base + 3U] | ((uint16_t)raw_points[base + 4U] << 8));
    uint16_t transformed_x = 0;
    uint16_t transformed_y = 0;
    bool valid = false;

    g_last_raw_state.id[g_last_raw_state.points] = track_id;
    g_last_raw_state.x[g_last_raw_state.points] = raw_x;
    g_last_raw_state.y[g_last_raw_state.points] = raw_y;
    ++g_last_raw_state.points;

    transform_point(raw_x, raw_y, &transformed_x, &transformed_y, &valid);
    if (!valid) {
      continue;
    }

    out_state->id[out_state->points] = track_id;
    out_state->x[out_state->points] = transformed_x;
    out_state->y[out_state->points] = transformed_y;
    ++out_state->points;
  }

  out_state->touched = out_state->points > 0U;
  g_last_state = *out_state;
  g_last_state.touched = g_last_state.points > 0U;
  g_last_raw_state.touched = g_last_raw_state.points > 0U;

  (void)write_reg8(kRegStatus, 0);
  log_calibration_samples();
  return true;
}

void touch_set_rotation(int rotation) {
  g_rotation = rotation & 3;
}

void touch_debug_dump_once_per_second(void) {
  const uint32_t now = millis();
  if (!g_touch_available || !g_last_state.touched || (now - g_last_debug_dump_ms) < 1000U) {
    return;
  }

  g_last_debug_dump_ms = now;
  ESP_LOGI(
      kTag,
      "touch points=%u first id=%u x=%u y=%u",
      (unsigned)g_last_state.points,
      (unsigned)g_last_state.id[0],
      (unsigned)g_last_state.x[0],
      (unsigned)g_last_state.y[0]);
}
