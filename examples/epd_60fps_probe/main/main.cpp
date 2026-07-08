#include <Arduino.h>
#include <Wire.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "epd_video.h"
#include "pca9535_min.h"
#include "t5s3_epd_pins.h"

namespace {

constexpr const char *kTag = "probe_main";
constexpr uint16_t kRegionPitch = t5s3_epd::kActiveWidth / 8U;
constexpr uint16_t kCheckerCell = 36;
constexpr uint16_t kRectWidth = 96;
constexpr uint16_t kRectHeight = 96;

Pca9535Min g_expander;
uint8_t *g_background = nullptr;

void put_pixel(uint8_t *buffer, int x, int y, bool white) {
  if (x < 0 || y < 0 || x >= t5s3_epd::kActiveWidth || y >= t5s3_epd::kActiveHeight) {
    return;
  }

  const size_t index = static_cast<size_t>(y) * kRegionPitch + static_cast<size_t>(x >> 3);
  const uint8_t mask = 0x80U >> (x & 7);
  if (white) {
    buffer[index] |= mask;
  } else {
    buffer[index] &= static_cast<uint8_t>(~mask);
  }
}

void fill_rect(uint8_t *buffer, int x, int y, int w, int h, bool white) {
  for (int py = y; py < (y + h); ++py) {
    for (int px = x; px < (x + w); ++px) {
      put_pixel(buffer, px, py, white);
    }
  }
}

void draw_hline(uint8_t *buffer, int x, int y, int w, bool white) {
  fill_rect(buffer, x, y, w, 1, white);
}

void draw_vline(uint8_t *buffer, int x, int y, int h, bool white) {
  fill_rect(buffer, x, y, 1, h, white);
}

void draw_frame(uint8_t *buffer, int x, int y, int w, int h, int thickness, bool white) {
  fill_rect(buffer, x, y, w, thickness, white);
  fill_rect(buffer, x, y + h - thickness, w, thickness, white);
  fill_rect(buffer, x, y, thickness, h, white);
  fill_rect(buffer, x + w - thickness, y, thickness, h, white);
}

void build_background(uint8_t *buffer) {
  memset(buffer, 0xFF, epd_video_get_backbuffer_size());

  draw_frame(buffer, 0, 0, t5s3_epd::kActiveWidth, t5s3_epd::kActiveHeight, 3, false);

  const int left_panel_x = 18;
  const int left_panel_y = 18;
  const int left_panel_w = 176;
  const int left_panel_h = t5s3_epd::kActiveHeight - 36;
  draw_frame(buffer, left_panel_x, left_panel_y, left_panel_w, left_panel_h, 2, false);

  for (int y = left_panel_y + 10; y < (left_panel_y + left_panel_h - 10); y += kCheckerCell) {
    for (int x = left_panel_x + 10; x < (left_panel_x + left_panel_w - 10); x += kCheckerCell) {
      const bool fill_black =
          ((((x - left_panel_x) / kCheckerCell) + ((y - left_panel_y) / kCheckerCell)) & 1U) == 0U;
      if (fill_black) {
        fill_rect(buffer, x, y, kCheckerCell / 2, kCheckerCell / 2, false);
      }
    }
  }

  const int guide_x = 226;
  const int guide_y = 32;
  const int guide_w = t5s3_epd::kActiveWidth - guide_x - 20;
  const int guide_h = t5s3_epd::kActiveHeight - 64;
  draw_frame(buffer, guide_x, guide_y, guide_w, guide_h, 2, false);

  for (int i = 1; i < 5; ++i) {
    const int y = guide_y + (guide_h * i) / 5;
    draw_hline(buffer, guide_x + 8, y, guide_w - 16, false);
  }

  for (int i = 1; i < 4; ++i) {
    const int x = guide_x + (guide_w * i) / 4;
    draw_vline(buffer, x, guide_y + 8, guide_h - 16, false);
  }

  for (int x = guide_x + 14; x < guide_x + guide_w - 14; x += 8) {
    draw_vline(buffer, x, t5s3_epd::kActiveHeight - 26, 8, false);
  }
}

void draw_moving_probe(uint8_t *buffer, int rect_x, int rect_y) {
  draw_frame(buffer, rect_x, rect_y, kRectWidth, kRectHeight, 3, false);
  draw_frame(buffer, rect_x + 12, rect_y + 12, kRectWidth - 24, kRectHeight - 24, 2, true);
  draw_frame(buffer, rect_x + 24, rect_y + 24, kRectWidth - 48, kRectHeight - 48, 2, false);
  draw_hline(buffer, rect_x + 8, rect_y + (kRectHeight / 2), kRectWidth - 16, false);
  draw_vline(buffer, rect_x + (kRectWidth / 2), rect_y + 8, kRectHeight - 16, false);
}

void compute_probe_position(uint32_t frame, int &rect_x, int &rect_y) {
  const int horizontal_span = t5s3_epd::kActiveWidth - kRectWidth - 24;
  const int vertical_span = t5s3_epd::kActiveHeight - kRectHeight - 24;
  const int horiz_phase = frame % (horizontal_span * 2U);
  const int vert_phase = (frame / 2U) % (vertical_span * 2U);

  rect_x = 12 + ((horiz_phase <= horizontal_span) ? horiz_phase : (horizontal_span * 2 - horiz_phase));
  rect_y = 12 + ((vert_phase <= vertical_span) ? vert_phase : (vertical_span * 2 - vert_phase));
}

void scan_i2c_bus() {
  char found[128] = {0};
  size_t offset = 0;

  for (uint8_t address = 1; address < 0x7F; ++address) {
    Wire.beginTransmission(address);
    if (Wire.endTransmission() == 0) {
      offset += static_cast<size_t>(snprintf(
          found + offset,
          sizeof(found) - offset,
          "%s0x%02X",
          offset == 0 ? "" : " ",
          address));
      if (offset >= sizeof(found)) {
        break;
      }
    }
  }

  ESP_LOGI(kTag, "I2C devices: %s", offset == 0 ? "none" : found);
}

void render_task(void *unused) {
  (void)unused;

  ESP_LOGI(kTag, "render task started on core %d", xPortGetCoreID());
  const TickType_t frame_ticks = pdMS_TO_TICKS(1000 / TARGET_FPS);
  uint64_t log_window_start = esp_timer_get_time();
  uint32_t log_frames = 0;
  int previous_rect_y = 12;

  for (uint32_t frame = 0;; ++frame) {
    const TickType_t frame_tick_start = xTaskGetTickCount();
    uint8_t *backbuffer = epd_video_get_backbuffer();
    memcpy(backbuffer, g_background, epd_video_get_backbuffer_size());

    int rect_x = 0;
    int rect_y = 0;
    compute_probe_position(frame, rect_x, rect_y);
    draw_moving_probe(backbuffer, rect_x, rect_y);

    if (frame == 0) {
      epd_video_flip(0, t5s3_epd::kActiveHeight);
    } else {
      int dirty_top = previous_rect_y;
      int dirty_bottom = previous_rect_y + kRectHeight - 1;

      if (rect_y < dirty_top) {
        dirty_top = rect_y;
      }
      if ((rect_y + kRectHeight - 1) > dirty_bottom) {
        dirty_bottom = rect_y + kRectHeight - 1;
      }

      dirty_top -= EPD_DIRTY_PADDING;
      dirty_bottom += EPD_DIRTY_PADDING;
      if (dirty_top < 0) {
        dirty_top = 0;
      }
      if (dirty_bottom >= t5s3_epd::kActiveHeight) {
        dirty_bottom = t5s3_epd::kActiveHeight - 1;
      }

      epd_video_flip(
          static_cast<uint16_t>(dirty_top),
          static_cast<uint16_t>(dirty_bottom - dirty_top + 1));
    }

    previous_rect_y = rect_y;
    ++log_frames;

    const uint64_t now = esp_timer_get_time();
    if ((now - log_window_start) >= 1000000ULL) {
      ESP_LOGI(
          kTag,
          "render=%lu fps vsync=%lu",
          static_cast<unsigned long>(log_frames),
          static_cast<unsigned long>(epd_video_get_vsync_count()));
      log_frames = 0;
      log_window_start = now;
    }

    if (frame_ticks > 0) {
      const TickType_t elapsed_ticks = xTaskGetTickCount() - frame_tick_start;
      if (elapsed_ticks < frame_ticks) {
        vTaskDelay(frame_ticks - elapsed_ticks);
      } else {
        vTaskDelay(1);
      }
    } else {
      vTaskDelay(1);
    }
  }
}

void log_memory() {
  ESP_LOGI(kTag, "board=%s", t5s3_epd::kBoardName);
  ESP_LOGI(kTag, "psram=%s", psramFound() ? "yes" : "no");
  ESP_LOGI(
      kTag,
      "heap internal free=%u bytes spiram free=%u bytes",
      static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
      static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));
}

bool init_probe() {
  Wire.begin(t5s3_epd::kI2cSda, t5s3_epd::kI2cScl);
  Wire.setClock(400000);
  Wire.setTimeout(100);

  scan_i2c_bus();

  if (!g_expander.begin(Wire, t5s3_epd::kPca9535Address)) {
    ESP_LOGE(kTag, "PCA9535 not found at 0x%02X", t5s3_epd::kPca9535Address);
    return false;
  }

  if (!g_expander.configureProbeDefaults()) {
    ESP_LOGE(kTag, "PCA9535 configure failed");
    return false;
  }

  uint8_t config0 = 0;
  uint8_t config1 = 0;
  uint8_t output0 = 0;
  uint8_t output1 = 0;
  if (g_expander.readState(config0, config1, output0, output1)) {
    ESP_LOGI(
        kTag,
        "PCA9535 config ok cfg0=0x%02X cfg1=0x%02X out0=0x%02X out1=0x%02X",
        config0,
        config1,
        output0,
        output1);
  }

  if (!epd_video_init(g_expander)) {
    ESP_LOGE(kTag, "epd_video_init failed");
    return false;
  }

  g_background = static_cast<uint8_t *>(
      heap_caps_malloc(epd_video_get_backbuffer_size(), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  if (g_background == nullptr) {
    g_background = static_cast<uint8_t *>(
        heap_caps_malloc(epd_video_get_backbuffer_size(), MALLOC_CAP_8BIT));
  }
  if (g_background == nullptr) {
    ESP_LOGE(kTag, "background allocation failed");
    return false;
  }
  build_background(g_background);

  if (!epd_video_power_on()) {
    ESP_LOGE(kTag, "epd_video_power_on failed");
    epd_video_shutdown();
    return false;
  }
  if (!epd_video_start()) {
    ESP_LOGE(kTag, "epd_video_start failed");
    epd_video_shutdown();
    return false;
  }
  return true;
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(1500);

  ESP_LOGI(kTag, "startup ok");
  log_memory();

  if (!init_probe()) {
    ESP_LOGE(kTag, "probe initialization failed");
    return;
  }

  const BaseType_t rc = xTaskCreatePinnedToCore(
      render_task,
      "render_task",
      8192,
      nullptr,
      2,
      nullptr,
      0);
  if (rc != pdPASS) {
    ESP_LOGE(kTag, "failed to create render task");
    epd_video_shutdown();
    return;
  }

  ESP_LOGI(
      kTag,
      "EPD probe running, region=%ux%u at (%u,%u)",
      t5s3_epd::kActiveWidth,
      t5s3_epd::kActiveHeight,
      t5s3_epd::kActiveX,
      t5s3_epd::kActiveY);
}

void loop() {
  delay(1000);
}
