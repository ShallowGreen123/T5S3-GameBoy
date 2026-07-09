#include <Arduino.h>
#include <Wire.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "epd_video.h"
#include "builtin_demo_rom.h"
#include "gbemu.h"
#include "pca9535_min.h"
#include "t5s3_epd_pins.h"

#if __has_include("rom/test_rom.h")
#include "rom/test_rom.h"
#define GBEMU_HAS_EMBEDDED_ROM 1
#else
#define GBEMU_HAS_EMBEDDED_ROM 0
#endif

namespace {

constexpr const char *kTag = "gb_probe";
constexpr uint8_t kMaxConsecutiveSkippedFrames = 2;

#ifndef GB_TEST_PULSE_BUTTON
#define GB_TEST_PULSE_BUTTON 0
#endif

#ifndef GB_TEST_PULSE_PERIOD_MS
#define GB_TEST_PULSE_PERIOD_MS 0
#endif

#ifndef GB_TEST_PULSE_WIDTH_MS
#define GB_TEST_PULSE_WIDTH_MS 120
#endif

enum class StaticScreen {
  kNoRom,
  kRomError,
  kRuntimeError,
};

struct TimingWindow {
  uint32_t count = 0;
  uint64_t total_us = 0;
  uint32_t max_us = 0;
};

Pca9535Min g_expander;
gbemu_t *g_emu = nullptr;
const char *g_idle_reason = nullptr;

const uint8_t *selected_rom_data() {
#if GBEMU_HAS_EMBEDDED_ROM
  return kTestRomData;
#else
  return builtin_demo_rom_data();
#endif
}

size_t selected_rom_size() {
#if GBEMU_HAS_EMBEDDED_ROM
  return kTestRomSize;
#else
  return builtin_demo_rom_size();
#endif
}

const char *selected_rom_source() {
#if GBEMU_HAS_EMBEDDED_ROM
  return "external header rom/test_rom.h";
#else
  return builtin_demo_rom_name();
#endif
}

void add_sample(TimingWindow &window, uint32_t value_us) {
  ++window.count;
  window.total_us += value_us;
  if (value_us > window.max_us) {
    window.max_us = value_us;
  }
}

uint32_t average_us(const TimingWindow &window) {
  return (window.count == 0U) ? 0U : static_cast<uint32_t>(window.total_us / window.count);
}

uint32_t fps_for_window(uint32_t frames, uint64_t elapsed_us) {
  return (elapsed_us == 0U) ? 0U : static_cast<uint32_t>((frames * 1000000ULL) / elapsed_us);
}

uint32_t max_u32(uint32_t a, uint32_t b) {
  return (a > b) ? a : b;
}

void put_pixel(uint8_t *buffer, int x, int y, bool white) {
  if (x < 0 || y < 0 || x >= static_cast<int>(GBEMU_FRAME_WIDTH) ||
      y >= static_cast<int>(GBEMU_FRAME_HEIGHT)) {
    return;
  }

  const size_t offset =
      (static_cast<size_t>(y) * GBEMU_FRAME_PITCH_BYTES) + static_cast<size_t>(x >> 3);
  const uint8_t mask = static_cast<uint8_t>(0x80U >> (x & 7));
  if (white) {
    buffer[offset] |= mask;
  } else {
    buffer[offset] &= static_cast<uint8_t>(~mask);
  }
}

void fill_rect(uint8_t *buffer, int x, int y, int w, int h, bool white) {
  for (int py = y; py < (y + h); ++py) {
    for (int px = x; px < (x + w); ++px) {
      put_pixel(buffer, px, py, white);
    }
  }
}

void draw_frame(uint8_t *buffer, int x, int y, int w, int h, int thickness, bool white) {
  fill_rect(buffer, x, y, w, thickness, white);
  fill_rect(buffer, x, y + h - thickness, w, thickness, white);
  fill_rect(buffer, x, y, thickness, h, white);
  fill_rect(buffer, x + w - thickness, y, thickness, h, white);
}

void draw_line(uint8_t *buffer, int x0, int y0, int x1, int y1, bool white) {
  int dx = abs(x1 - x0);
  int sx = (x0 < x1) ? 1 : -1;
  int dy = -abs(y1 - y0);
  int sy = (y0 < y1) ? 1 : -1;
  int err = dx + dy;

  while (true) {
    put_pixel(buffer, x0, y0, white);
    if (x0 == x1 && y0 == y1) {
      break;
    }
    const int e2 = err * 2;
    if (e2 >= dy) {
      err += dy;
      x0 += sx;
    }
    if (e2 <= dx) {
      err += dx;
      y0 += sy;
    }
  }
}

void build_static_screen(uint8_t *buffer, StaticScreen screen) {
  memset(buffer, 0xFF, GBEMU_FRAMEBUFFER_SIZE);
  draw_frame(buffer, 0, 0, GBEMU_FRAME_WIDTH, GBEMU_FRAME_HEIGHT, 3, false);
  draw_frame(buffer, 16, 16, GBEMU_FRAME_WIDTH - 32, GBEMU_FRAME_HEIGHT - 32, 2, false);

  for (int y = 48; y < static_cast<int>(GBEMU_FRAME_HEIGHT) - 48; y += 36) {
    fill_rect(buffer, 32, y, 12, 12, false);
    fill_rect(buffer, static_cast<int>(GBEMU_FRAME_WIDTH) - 44, y, 12, 12, false);
  }

  switch (screen) {
    case StaticScreen::kNoRom:
      draw_frame(buffer, 126, 126, 180, 228, 4, false);
      fill_rect(buffer, 150, 106, 132, 30, false);
      fill_rect(buffer, 166, 168, 100, 110, false);
      draw_line(buffer, 108, 364, 324, 116, false);
      draw_line(buffer, 118, 374, 334, 126, false);
      fill_rect(buffer, 110, 382, 212, 24, false);
      break;

    case StaticScreen::kRomError:
      draw_frame(buffer, 104, 104, 224, 272, 4, false);
      fill_rect(buffer, 148, 148, 136, 36, false);
      fill_rect(buffer, 148, 296, 136, 36, false);
      draw_line(buffer, 132, 148, 300, 332, false);
      draw_line(buffer, 300, 148, 132, 332, false);
      draw_line(buffer, 132, 158, 290, 332, false);
      draw_line(buffer, 290, 148, 132, 322, false);
      break;

    case StaticScreen::kRuntimeError:
      fill_rect(buffer, 72, 72, 288, 48, false);
      fill_rect(buffer, 72, 360, 288, 48, false);
      draw_frame(buffer, 118, 132, 196, 176, 4, false);
      fill_rect(buffer, 192, 152, 48, 100, false);
      fill_rect(buffer, 192, 268, 48, 48, false);
      break;
  }
}

void present_static_screen(StaticScreen screen) {
  uint8_t *backbuffer = epd_video_get_backbuffer();
  if (backbuffer == nullptr) {
    return;
  }

  build_static_screen(backbuffer, screen);
  epd_video_flip(0, t5s3_epd::kActiveHeight);
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
          (offset == 0U) ? "" : " ",
          address));
      if (offset >= sizeof(found)) {
        break;
      }
    }
  }

  ESP_LOGI(kTag, "I2C devices: %s", (offset == 0U) ? "none" : found);
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

void on_shutdown() {
  epd_video_shutdown();
}

bool init_display_pipeline() {
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
  if (!epd_video_init(g_expander)) {
    ESP_LOGE(kTag, "epd_video_init failed");
    return false;
  }
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

uint8_t compute_input_mask(uint32_t frame_index) {
#if GB_TEST_PULSE_BUTTON != 0 && GB_TEST_PULSE_PERIOD_MS > 0
  const uint32_t period_frames = max_u32(1U, (GB_TEST_PULSE_PERIOD_MS * TARGET_FPS) / 1000U);
  const uint32_t width_frames = max_u32(1U, (GB_TEST_PULSE_WIDTH_MS * TARGET_FPS) / 1000U);
  return ((frame_index % period_frames) < width_frames) ? static_cast<uint8_t>(GB_TEST_PULSE_BUTTON) : 0U;
#else
  (void)frame_index;
  return 0U;
#endif
}

void render_task(void *unused) {
  (void)unused;

  uint32_t last_vsync = epd_video_get_vsync_count();
  uint8_t consecutive_skips = 0;
  uint64_t log_window_start = esp_timer_get_time();
  uint32_t emu_frames = 0;
  uint32_t render_frames = 0;
  uint32_t skipped_frames = 0;
  uint32_t missed_vsyncs = 0;
  TimingWindow run_window = {};
  TimingWindow draw_window = {};
  TimingWindow flip_window = {};

  ESP_LOGI(kTag, "render task started on core %d", xPortGetCoreID());

  while (true) {
    const uint32_t vsync_before = epd_video_get_vsync_count();
    const uint32_t vsync_gap = vsync_before - last_vsync;
    const bool skip_render = (vsync_gap > 0U) && (consecutive_skips < kMaxConsecutiveSkippedFrames);
    gbemu_frame_stats_t frame_stats = {};
    uint8_t *backbuffer = skip_render ? nullptr : epd_video_get_backbuffer();

    if (vsync_gap > 0U) {
      missed_vsyncs += vsync_gap;
    }

    if (!gbemu_run_frame(
            g_emu,
            backbuffer,
            epd_video_get_backbuffer_size(),
            compute_input_mask(emu_frames),
            skip_render,
            &frame_stats)) {
      ESP_LOGE(
          kTag,
          "emulator halted: %s addr=0x%04X",
          gbemu_status_string(gbemu_get_status(g_emu)),
          gbemu_get_last_error_addr(g_emu));
      present_static_screen(StaticScreen::kRuntimeError);
      break;
    }

    add_sample(run_window, frame_stats.run_us);
    ++emu_frames;

    if (skip_render) {
      ++skipped_frames;
      consecutive_skips = static_cast<uint8_t>(consecutive_skips + 1U);
      last_vsync = epd_video_get_vsync_count();
    } else {
      const int64_t flip_started_at = esp_timer_get_time();
      epd_video_flip(0, t5s3_epd::kActiveHeight);
      add_sample(flip_window, (uint32_t)(esp_timer_get_time() - flip_started_at));
      add_sample(draw_window, frame_stats.draw_us);
      ++render_frames;
      consecutive_skips = 0;
      last_vsync = epd_video_get_vsync_count();
    }

    const uint64_t now = esp_timer_get_time();
    if ((now - log_window_start) >= 1000000ULL) {
      const uint64_t elapsed_us = now - log_window_start;
      ESP_LOGI(
          kTag,
          "emu=%lu render=%lu skip=%lu emu_fps=%lu render_fps=%lu missed_vsync=%lu vsync=%lu heap=%u psram=%u run(us avg/max)=%lu/%lu draw=%lu/%lu flip=%lu/%lu",
          static_cast<unsigned long>(emu_frames),
          static_cast<unsigned long>(render_frames),
          static_cast<unsigned long>(skipped_frames),
          static_cast<unsigned long>(fps_for_window(emu_frames, elapsed_us)),
          static_cast<unsigned long>(fps_for_window(render_frames, elapsed_us)),
          static_cast<unsigned long>(missed_vsyncs),
          static_cast<unsigned long>(epd_video_get_vsync_count()),
          static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
          static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)),
          static_cast<unsigned long>(average_us(run_window)),
          static_cast<unsigned long>(run_window.max_us),
          static_cast<unsigned long>(average_us(draw_window)),
          static_cast<unsigned long>(draw_window.max_us),
          static_cast<unsigned long>(average_us(flip_window)),
          static_cast<unsigned long>(flip_window.max_us));
      log_window_start = now;
      emu_frames = 0;
      render_frames = 0;
      skipped_frames = 0;
      missed_vsyncs = 0;
      run_window = {};
      draw_window = {};
      flip_window = {};
    }
  }

  vTaskDelete(nullptr);
}

void enter_idle_state(const char *reason, StaticScreen screen) {
  g_idle_reason = reason;
  ESP_LOGW(kTag, "%s", reason);
  present_static_screen(screen);
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(1500);

  ESP_LOGI(kTag, "startup ok");
  log_memory();

  if (esp_register_shutdown_handler(on_shutdown) != ESP_OK) {
    ESP_LOGW(kTag, "failed to register shutdown handler");
  }

  if (!init_display_pipeline()) {
    ESP_LOGE(kTag, "display pipeline initialization failed");
    return;
  }

  if (epd_video_get_backbuffer_size() != GBEMU_FRAMEBUFFER_SIZE) {
    enter_idle_state("Unexpected EPD backbuffer size for 432x480 GameBoy output.", StaticScreen::kRomError);
    return;
  }

  g_emu = gbemu_create();
  if (g_emu == nullptr) {
    enter_idle_state("Unable to allocate emulator context.", StaticScreen::kRomError);
    return;
  }

  ESP_LOGI(kTag, "ROM source: %s", selected_rom_source());

  const gbemu_status_t init_status = gbemu_init(g_emu, selected_rom_data(), selected_rom_size());
  if (init_status != GBEMU_STATUS_OK) {
    const bool no_rom = (init_status == GBEMU_STATUS_NO_ROM);
    enter_idle_state(
        no_rom
            ? "No ROM source is available."
            : gbemu_status_string(init_status),
        no_rom ? StaticScreen::kNoRom : StaticScreen::kRomError);
    gbemu_destroy(g_emu);
    g_emu = nullptr;
    return;
  }

  ESP_LOGI(
      kTag,
      "ROM ready: source=%s title=\"%s\" frame=%ux%u scale=%u rotate=90CW",
      selected_rom_source(),
      gbemu_get_rom_title(g_emu),
      GBEMU_SOURCE_WIDTH,
      GBEMU_SOURCE_HEIGHT,
      GBEMU_SCALE);

  const BaseType_t rc = xTaskCreatePinnedToCore(
      render_task,
      "gb_render",
      12288,
      nullptr,
      2,
      nullptr,
      0);
  if (rc != pdPASS) {
    enter_idle_state("Failed to create GameBoy render task.", StaticScreen::kRuntimeError);
    gbemu_destroy(g_emu);
    g_emu = nullptr;
    return;
  }
}

void loop() {
  static uint32_t last_idle_log_ms = 0;

  if (g_idle_reason != nullptr && (millis() - last_idle_log_ms) >= 5000U) {
    ESP_LOGW(kTag, "%s", g_idle_reason);
    last_idle_log_ms = millis();
  }

  delay(250);
}
