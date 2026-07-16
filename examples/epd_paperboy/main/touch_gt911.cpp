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
constexpr uint16_t kFallbackRawWidth = 540;
constexpr uint16_t kFallbackRawHeight = 960;

bool g_touch_available = false;
uint8_t g_touch_address = 0;
int g_rotation = 0;
uint16_t g_raw_max_x = kFallbackRawWidth;
uint16_t g_raw_max_y = kFallbackRawHeight;
char g_product_id[5] = "----";
touch_state_t g_last_state = {};
touch_state_t g_last_raw_state = {};
uint32_t g_last_debug_dump_ms = 0;
uint32_t g_last_calibration_dump_ms = 0;
uint32_t g_status_polls = 0;
uint32_t g_read_errors = 0;
uint8_t g_last_status = 0;

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

bool reset_and_probe(uint8_t address) {
  pinMode(t5s3_epd::kTouchRst, OUTPUT);
  pinMode(t5s3_epd::kTouchInt, OUTPUT);
  digitalWrite(t5s3_epd::kTouchRst, LOW);
  digitalWrite(t5s3_epd::kTouchInt, address == kPrimaryAddress ? LOW : HIGH);
  delayMicroseconds(120);
  digitalWrite(t5s3_epd::kTouchRst, HIGH);
  delay(18);
  pinMode(t5s3_epd::kTouchInt, INPUT);
  delay(20);

  Wire.beginTransmission(address);
  return Wire.endTransmission() == 0;
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
      const int32_t swapped_span = span_x;
      span_x = span_y;
      span_y = swapped_span;
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
      const int32_t swapped_span = span_x;
      span_x = span_y;
      span_y = swapped_span;
      break;
    }
    default:
      break;
  }

  // GT911 is configured in the device's native portrait space (540x960).
  // Validate against the touch controller's own output dimensions rather
  // than the EPD's electrically scanned landscape dimensions (960x540).
  *valid = x >= 0 && y >= 0 && x < span_x && y < span_y;
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
  g_raw_max_x = kFallbackRawWidth;
  g_raw_max_y = kFallbackRawHeight;
  g_status_polls = 0;
  g_read_errors = 0;
  g_last_status = 0;

  // The shared board I2C bus is already running for PCA9535/TPS65185.
  Wire.setClock(400000);
  Wire.setTimeout(kI2cTimeoutMs);

  for (uint8_t candidate : {kPrimaryAddress, kFallbackAddress}) {
    if (reset_and_probe(candidate)) {
      g_touch_address = candidate;
      break;
    }
  }

  if (g_touch_address == 0U) {
    ESP_LOGW(kTag, "GT911 probe failed at 0x%02X/0x%02X", kPrimaryAddress, kFallbackAddress);
    return false;
  }

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
    g_raw_max_x = kFallbackRawWidth;
    g_raw_max_y = kFallbackRawHeight;
  }

  // Discard any stale packet left over from reset.
  (void)write_reg8(kRegStatus, 0);

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
    ++g_read_errors;
    return false;
  }
  ++g_status_polls;
  g_last_status = status;

  const uint8_t point_count = (uint8_t)(status & kTouchCountMask);
  if (point_count == 0U) {
    if ((status & kReadyMask) != 0U) {
      (void)write_reg8(kRegStatus, 0);
    }
    g_last_state = *out_state;
    g_last_raw_state = *out_state;
    return true;
  }
  if (point_count > 5U) {
    ESP_LOGW(kTag, "ignoring malformed GT911 packet count=%u", (unsigned)point_count);
    (void)write_reg8(kRegStatus, 0);
    return false;
  }

  if (!read_regs(kRegFirstPoint, raw_points, (size_t)point_count * 8U)) {
    ++g_read_errors;
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
  if (!g_touch_available || (now - g_last_debug_dump_ms) < 1000U) {
    return;
  }

  g_last_debug_dump_ms = now;
  if (g_last_state.touched) {
    ESP_LOGI(
        kTag,
        "polls=%lu errors=%lu status=0x%02X int=%d points=%u first id=%u x=%u y=%u",
        (unsigned long)g_status_polls,
        (unsigned long)g_read_errors,
        (unsigned)g_last_status,
        digitalRead(t5s3_epd::kTouchInt),
        (unsigned)g_last_state.points,
        (unsigned)g_last_state.id[0],
        (unsigned)g_last_state.x[0],
        (unsigned)g_last_state.y[0]);
  } else {
    ESP_LOGI(
        kTag,
        "polls=%lu errors=%lu status=0x%02X int=%d points=0",
        (unsigned long)g_status_polls,
        (unsigned long)g_read_errors,
        (unsigned)g_last_status,
        digitalRead(t5s3_epd::kTouchInt));
  }
}
