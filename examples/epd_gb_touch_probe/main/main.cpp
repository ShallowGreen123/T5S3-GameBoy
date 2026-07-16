#include <Arduino.h>
#include <Wire.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <string.h>

#include "builtin_demo_rom.h"
#include "epd_video.h"
#include "gbemu.h"
#include "mono_canvas.h"
#include "pca9535_min.h"
#include "touch_gt911.h"
#include "t5s3_epd_pins.h"
#include "virtual_controls.h"

#if __has_include("rom/test_rom.h")
#include "rom/test_rom.h"
#define GBEMU_HAS_EMBEDDED_ROM 1
#else
#define GBEMU_HAS_EMBEDDED_ROM 0
#endif

namespace {

constexpr const char *kTag = "gb_touch";
constexpr uint8_t kMaxConsecutiveSkippedFrames = 2;
constexpr uint16_t kGameOriginX = 432;
constexpr uint16_t kGameOriginY = 30;
constexpr uint16_t kScreenPitchBytes = t5s3_epd::kActiveWidth / 8U;
constexpr size_t kScreenBufferBytes =
    (size_t)kScreenPitchBytes * (size_t)t5s3_epd::kActiveHeight;

static_assert(GBEMU_FRAME_WIDTH == 432U, "unexpected GB frame width");
static_assert(GBEMU_FRAME_HEIGHT == 480U, "unexpected GB frame height");
static_assert((kGameOriginX % 8U) == 0U, "game region must stay byte aligned");

struct TimingWindow {
  uint32_t count = 0;
  uint64_t total_us = 0;
  uint32_t max_us = 0;
};

enum class MessageScreen {
  kInfo,
  kError,
};

Pca9535Min g_expander;
gbemu_t *g_emu = nullptr;
uint8_t *g_background = nullptr;
uint8_t *g_gb_frame = nullptr;
uint8_t *g_quicksave_buffer = nullptr;
size_t g_quicksave_buffer_size = 0;
bool g_quicksave_valid = false;
bool g_touch_available = false;
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
  return (window.count == 0U) ? 0U : (uint32_t)(window.total_us / window.count);
}

uint32_t fps_for_window(uint32_t frames, uint64_t elapsed_us) {
  return (elapsed_us == 0U) ? 0U : (uint32_t)((frames * 1000000ULL) / elapsed_us);
}

uint8_t *alloc_framebuffer(size_t size, bool prefer_internal) {
  uint8_t *buffer = nullptr;

  if (prefer_internal) {
    buffer = (uint8_t *)heap_caps_malloc(size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (buffer == nullptr) {
      buffer = (uint8_t *)heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
  } else {
    buffer = (uint8_t *)heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (buffer == nullptr) {
      buffer = (uint8_t *)heap_caps_malloc(size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
  }

  if (buffer == nullptr) {
    buffer = (uint8_t *)heap_caps_malloc(size, MALLOC_CAP_8BIT);
  }
  return buffer;
}

void append_label(char *buffer, size_t buffer_size, size_t &offset, const char *label) {
  if (offset >= buffer_size) {
    return;
  }

  offset += (size_t)snprintf(
      buffer + offset,
      buffer_size - offset,
      "%s%s",
      offset == 0U ? "" : "|",
      label);
}

void format_buttons(uint8_t buttons, char *buffer, size_t buffer_size) {
  size_t offset = 0;
  buffer[0] = '\0';
  if (buttons == 0U) {
    (void)snprintf(buffer, buffer_size, "NONE");
    return;
  }
  if ((buttons & GB_BTN_UP) != 0U) {
    append_label(buffer, buffer_size, offset, "UP");
  }
  if ((buttons & GB_BTN_DOWN) != 0U) {
    append_label(buffer, buffer_size, offset, "DOWN");
  }
  if ((buttons & GB_BTN_LEFT) != 0U) {
    append_label(buffer, buffer_size, offset, "LEFT");
  }
  if ((buttons & GB_BTN_RIGHT) != 0U) {
    append_label(buffer, buffer_size, offset, "RIGHT");
  }
  if ((buttons & GB_BTN_A) != 0U) {
    append_label(buffer, buffer_size, offset, "A");
  }
  if ((buttons & GB_BTN_B) != 0U) {
    append_label(buffer, buffer_size, offset, "B");
  }
  if ((buttons & GB_BTN_SELECT) != 0U) {
    append_label(buffer, buffer_size, offset, "SELECT");
  }
  if ((buttons & GB_BTN_START) != 0U) {
    append_label(buffer, buffer_size, offset, "START");
  }
}

void format_actions(uint32_t actions, char *buffer, size_t buffer_size) {
  size_t offset = 0;
  buffer[0] = '\0';
  if (actions == 0U) {
    (void)snprintf(buffer, buffer_size, "NONE");
    return;
  }
  if ((actions & SYS_ACTION_CLEAR) != 0U) {
    append_label(buffer, buffer_size, offset, "CLEAR");
  }
  if ((actions & SYS_ACTION_PAUSE) != 0U) {
    append_label(buffer, buffer_size, offset, "PAUSE");
  }
  if ((actions & SYS_ACTION_SAVE) != 0U) {
    append_label(buffer, buffer_size, offset, "SAVE");
  }
  if ((actions & SYS_ACTION_LOAD) != 0U) {
    append_label(buffer, buffer_size, offset, "LOAD");
  }
  if ((actions & SYS_ACTION_RESET_OPTIONAL) != 0U) {
    append_label(buffer, buffer_size, offset, "RESET");
  }
}

void blit_game_frame(uint8_t *framebuffer) {
  const size_t dest_offset = (size_t)(kGameOriginX / 8U);
  for (uint16_t row = 0; row < GBEMU_FRAME_HEIGHT; ++row) {
    uint8_t *dst = framebuffer + ((size_t)(row + kGameOriginY) * kScreenPitchBytes) + dest_offset;
    const uint8_t *src = g_gb_frame + ((size_t)row * GBEMU_FRAME_PITCH_BYTES);
    memcpy(dst, src, GBEMU_FRAME_PITCH_BYTES);
  }
}

void draw_pause_badge(uint8_t *framebuffer) {
  mono_fill_rect(
      framebuffer,
      kScreenPitchBytes,
      t5s3_epd::kActiveWidth,
      t5s3_epd::kActiveHeight,
      726,
      36,
      126,
      34,
      true);
  mono_draw_frame(
      framebuffer,
      kScreenPitchBytes,
      t5s3_epd::kActiveWidth,
      t5s3_epd::kActiveHeight,
      726,
      36,
      126,
      34,
      2,
      false);
  mono_draw_text(
      framebuffer,
      kScreenPitchBytes,
      t5s3_epd::kActiveWidth,
      t5s3_epd::kActiveHeight,
      742,
      47,
      "PAUSED",
      1,
      false);
}

void draw_touch_markers(uint8_t *framebuffer, const touch_state_t *touch) {
  if (touch == nullptr || !touch->touched) {
    return;
  }

  for (uint8_t i = 0; i < touch->points; ++i) {
    const int x = touch->x[i];
    const int y = touch->y[i];
    mono_draw_line(
        framebuffer,
        kScreenPitchBytes,
        t5s3_epd::kActiveWidth,
        t5s3_epd::kActiveHeight,
        x - 8,
        y,
        x + 8,
        y,
        false);
    mono_draw_line(
        framebuffer,
        kScreenPitchBytes,
        t5s3_epd::kActiveWidth,
        t5s3_epd::kActiveHeight,
        x,
        y - 8,
        x,
        y + 8,
        false);
  }
}

void compose_scene(uint8_t *framebuffer, bool paused, const touch_state_t *touch) {
  if (framebuffer == nullptr || g_background == nullptr || g_gb_frame == nullptr) {
    return;
  }

  memcpy(framebuffer, g_background, kScreenBufferBytes);
  blit_game_frame(framebuffer);
  if (paused) {
    draw_pause_badge(framebuffer);
  }
#if TOUCH_CALIBRATION_MODE
  draw_touch_markers(framebuffer, touch);
#else
  (void)touch;
#endif
}

void present_message_screen(const char *line1, const char *line2, MessageScreen style) {
  uint8_t *backbuffer = epd_video_get_backbuffer();
  if (backbuffer == nullptr) {
    return;
  }

  mono_clear(backbuffer, kScreenBufferBytes, true);
  mono_draw_frame(
      backbuffer,
      kScreenPitchBytes,
      t5s3_epd::kActiveWidth,
      t5s3_epd::kActiveHeight,
      0,
      0,
      t5s3_epd::kActiveWidth,
      t5s3_epd::kActiveHeight,
      3,
      false);
  mono_draw_frame(
      backbuffer,
      kScreenPitchBytes,
      t5s3_epd::kActiveWidth,
      t5s3_epd::kActiveHeight,
      190,
      150,
      580,
      220,
      3,
      false);
  if (style == MessageScreen::kError) {
    mono_fill_rect(
        backbuffer,
        kScreenPitchBytes,
        t5s3_epd::kActiveWidth,
        t5s3_epd::kActiveHeight,
        218,
        178,
        524,
        30,
        false);
  }
  mono_draw_text(
      backbuffer,
      kScreenPitchBytes,
      t5s3_epd::kActiveWidth,
      t5s3_epd::kActiveHeight,
      232,
      220,
      line1,
      3,
      style == MessageScreen::kError);
  mono_draw_text(
      backbuffer,
      kScreenPitchBytes,
      t5s3_epd::kActiveWidth,
      t5s3_epd::kActiveHeight,
      232,
      286,
      line2,
      2,
      false);
  epd_video_flip(0, t5s3_epd::kActiveHeight);
}

void build_background(void) {
  mono_clear(g_background, kScreenBufferBytes, true);
  vc_draw_static_overlay(g_background);
  mono_draw_text(
      g_background,
      kScreenPitchBytes,
      t5s3_epd::kActiveWidth,
      t5s3_epd::kActiveHeight,
      444,
      516,
      g_touch_available ? "GT911 READY" : "GT911 OFF",
      1,
      false);
}

void scan_i2c_bus() {
  char found[128] = {0};
  size_t offset = 0;

  for (uint8_t address = 1; address < 0x7F; ++address) {
    Wire.beginTransmission(address);
    if (Wire.endTransmission() == 0) {
      offset += (size_t)snprintf(
          found + offset,
          sizeof(found) - offset,
          "%s0x%02X",
          offset == 0U ? "" : " ",
          address);
      if (offset >= sizeof(found)) {
        break;
      }
    }
  }

  ESP_LOGI(kTag, "I2C devices: %s", offset == 0U ? "none" : found);
}

void log_memory() {
  ESP_LOGI(kTag, "board=%s", t5s3_epd::kBoardName);
  ESP_LOGI(kTag, "psram=%s", psramFound() ? "yes" : "no");
  ESP_LOGI(
      kTag,
      "heap internal free=%u bytes spiram free=%u bytes",
      (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
      (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
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

bool allocate_runtime_buffers() {
  g_background = alloc_framebuffer(kScreenBufferBytes, false);
  g_gb_frame = alloc_framebuffer(GBEMU_FRAMEBUFFER_SIZE, true);

  if (g_background == nullptr || g_gb_frame == nullptr) {
    ESP_LOGE(kTag, "buffer allocation failed bg=%p gb=%p", g_background, g_gb_frame);
    return false;
  }

  memset(g_gb_frame, 0xFF, GBEMU_FRAMEBUFFER_SIZE);
  return true;
}

void allocate_quicksave_buffer() {
  g_quicksave_buffer_size = gbemu_get_state_size(g_emu);
  if (g_quicksave_buffer_size == 0U) {
    ESP_LOGW(kTag, "quick save snapshot size is unavailable");
    return;
  }

  g_quicksave_buffer = (uint8_t *)heap_caps_malloc(
      g_quicksave_buffer_size,
      MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (g_quicksave_buffer == nullptr) {
    g_quicksave_buffer = (uint8_t *)heap_caps_malloc(
        g_quicksave_buffer_size,
        MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  }
  if (g_quicksave_buffer == nullptr) {
    ESP_LOGW(kTag, "quick save buffer allocation failed");
    return;
  }

  memset(g_quicksave_buffer, 0, g_quicksave_buffer_size);
  ESP_LOGI(kTag, "quick save buffer ready: %u bytes", (unsigned)g_quicksave_buffer_size);
}

void perform_clear_cycle(bool paused, const touch_state_t *touch) {
  uint8_t *backbuffer = epd_video_get_backbuffer();
  if (backbuffer == nullptr) {
    return;
  }

  ESP_LOGI(kTag, "clear cycle start");
  mono_clear(backbuffer, kScreenBufferBytes, true);
  epd_video_flip(0, t5s3_epd::kActiveHeight);
  mono_clear(backbuffer, kScreenBufferBytes, false);
  epd_video_flip(0, t5s3_epd::kActiveHeight);
  mono_clear(backbuffer, kScreenBufferBytes, true);
  epd_video_flip(0, t5s3_epd::kActiveHeight);
  compose_scene(backbuffer, paused, touch);
  epd_video_flip(0, t5s3_epd::kActiveHeight);
  ESP_LOGI(kTag, "clear cycle done");
}

void render_task(void *unused) {
  (void)unused;

  uint32_t last_vsync = epd_video_get_vsync_count();
  uint8_t consecutive_skips = 0U;
  uint8_t last_buttons = 0xFFU;
  bool paused = false;
  uint64_t log_window_start = esp_timer_get_time();
  uint32_t emu_frames = 0U;
  uint32_t render_frames = 0U;
  uint32_t skipped_frames = 0U;
  uint32_t touch_polls = 0U;
  uint32_t touch_ok_reads = 0U;
  uint32_t missed_vsyncs = 0U;
  TimingWindow touch_window = {};
  TimingWindow map_window = {};
  TimingWindow run_window = {};
  TimingWindow draw_window = {};
  TimingWindow flip_window = {};

  ESP_LOGI(kTag, "render task started on core %d", xPortGetCoreID());

  {
    uint8_t *backbuffer = epd_video_get_backbuffer();
    compose_scene(backbuffer, false, nullptr);
    epd_video_flip(0, t5s3_epd::kActiveHeight);
  }

  while (true) {
    touch_state_t touch = {};
    const int64_t touch_started_at = esp_timer_get_time();
    const bool touch_ok = g_touch_available ? touch_read(&touch) : false;
    add_sample(touch_window, (uint32_t)(esp_timer_get_time() - touch_started_at));
    ++touch_polls;
    if (touch_ok) {
      ++touch_ok_reads;
      touch_debug_dump_once_per_second();
    }

    const int64_t map_started_at = esp_timer_get_time();
    const uint8_t buttons = touch_ok ? vc_map_gb_buttons(&touch) : 0U;
    const uint32_t actions = touch_ok ? vc_map_system_actions(&touch) : 0U;
    add_sample(map_window, (uint32_t)(esp_timer_get_time() - map_started_at));

    if (buttons != last_buttons) {
      char button_string[64];
      format_buttons(buttons, button_string, sizeof(button_string));
      ESP_LOGI(kTag, "buttons=0x%02X %s", buttons, button_string);
      last_buttons = buttons;
    }
    if (actions != 0U) {
      char action_string[64];
      format_actions(actions, action_string, sizeof(action_string));
      ESP_LOGI(kTag, "actions=0x%02lX %s", (unsigned long)actions, action_string);
    }

    bool force_redraw = false;
    bool force_full_redraw = false;

    if ((actions & SYS_ACTION_PAUSE) != 0U) {
      paused = !paused;
      force_redraw = true;
      force_full_redraw = true;
      ESP_LOGI(kTag, "pause=%s", paused ? "on" : "off");
    }

    if ((actions & SYS_ACTION_SAVE) != 0U) {
      if (g_quicksave_buffer != nullptr &&
          gbemu_save_state(g_emu, g_quicksave_buffer, g_quicksave_buffer_size)) {
        g_quicksave_valid = true;
        ESP_LOGI(kTag, "quick save stored");
      } else {
        ESP_LOGW(kTag, "quick save unavailable");
      }
    }

    if ((actions & SYS_ACTION_LOAD) != 0U) {
      if (g_quicksave_valid &&
          g_quicksave_buffer != nullptr &&
          gbemu_load_state(g_emu, g_quicksave_buffer, g_quicksave_buffer_size)) {
        paused = false;
        force_redraw = true;
        force_full_redraw = true;
        ESP_LOGI(kTag, "quick load restored");
      } else {
        ESP_LOGW(kTag, "quick load unavailable");
      }
    }

    if ((actions & SYS_ACTION_RESET_OPTIONAL) != 0U) {
      gbemu_reset(g_emu);
      paused = false;
      force_redraw = true;
      force_full_redraw = true;
      ESP_LOGI(kTag, "emulator reset");
    }

    if ((actions & SYS_ACTION_CLEAR) != 0U) {
      perform_clear_cycle(paused, touch_ok ? &touch : nullptr);
      force_redraw = true;
      force_full_redraw = true;
      last_vsync = epd_video_get_vsync_count();
    }

#if TOUCH_CALIBRATION_MODE
    if (touch_ok && touch.touched) {
      force_redraw = true;
      force_full_redraw = true;
    }
#endif

    if (!paused) {
      const uint32_t vsync_before = epd_video_get_vsync_count();
      const uint32_t vsync_gap = vsync_before - last_vsync;
      const bool skip_render =
          !force_redraw && vsync_gap > 0U && consecutive_skips < kMaxConsecutiveSkippedFrames;
      gbemu_frame_stats_t frame_stats = {};

      if (vsync_gap > 0U) {
        missed_vsyncs += vsync_gap;
      }

      if (!gbemu_run_frame(
              g_emu,
              skip_render ? nullptr : g_gb_frame,
              GBEMU_FRAMEBUFFER_SIZE,
              buttons,
              skip_render,
              &frame_stats)) {
        ESP_LOGE(
            kTag,
            "emulator halted: %s addr=0x%04X",
            gbemu_status_string(gbemu_get_status(g_emu)),
            gbemu_get_last_error_addr(g_emu));
        present_message_screen("EMU ERROR", "CHECK SERIAL LOG", MessageScreen::kError);
        break;
      }

      add_sample(run_window, frame_stats.run_us);
      ++emu_frames;

      if (skip_render) {
        ++skipped_frames;
        consecutive_skips = (uint8_t)(consecutive_skips + 1U);
        last_vsync = epd_video_get_vsync_count();
        vTaskDelay(1);
      } else {
        uint8_t *backbuffer = epd_video_get_backbuffer();
        if (backbuffer == nullptr) {
          present_message_screen("NO BACKBUFFER", "CHECK EPD", MessageScreen::kError);
          break;
        }
        compose_scene(backbuffer, false, touch_ok ? &touch : nullptr);
        add_sample(draw_window, frame_stats.draw_us);

        const int64_t flip_started_at = esp_timer_get_time();
        epd_video_flip(
            force_full_redraw ? 0U : kGameOriginY,
            force_full_redraw ? t5s3_epd::kActiveHeight : GBEMU_FRAME_HEIGHT);
        add_sample(flip_window, (uint32_t)(esp_timer_get_time() - flip_started_at));
        ++render_frames;
        consecutive_skips = 0U;
        last_vsync = epd_video_get_vsync_count();
      }
    } else if (force_redraw) {
      uint8_t *backbuffer = epd_video_get_backbuffer();
      if (backbuffer == nullptr) {
        present_message_screen("NO BACKBUFFER", "CHECK EPD", MessageScreen::kError);
        break;
      }

      compose_scene(backbuffer, true, touch_ok ? &touch : nullptr);
      const int64_t flip_started_at = esp_timer_get_time();
      epd_video_flip(
          force_full_redraw ? 0U : kGameOriginY,
          force_full_redraw ? t5s3_epd::kActiveHeight : GBEMU_FRAME_HEIGHT);
      add_sample(flip_window, (uint32_t)(esp_timer_get_time() - flip_started_at));
      ++render_frames;
      last_vsync = epd_video_get_vsync_count();
    } else {
      vTaskDelay(1);
    }

    const uint64_t now = esp_timer_get_time();
    if ((now - log_window_start) >= 1000000ULL) {
      const uint64_t elapsed_us = now - log_window_start;
      ESP_LOGI(
          kTag,
          "touch=%lu ok=%lu emu=%lu render=%lu skip=%lu paused=%s touch_fps=%lu emu_fps=%lu render_fps=%lu missed_vsync=%lu heap=%u psram=%u touch(us avg/max)=%lu/%lu map=%lu/%lu run=%lu/%lu draw=%lu/%lu flip=%lu/%lu",
          (unsigned long)touch_polls,
          (unsigned long)touch_ok_reads,
          (unsigned long)emu_frames,
          (unsigned long)render_frames,
          (unsigned long)skipped_frames,
          paused ? "yes" : "no",
          (unsigned long)fps_for_window(touch_polls, elapsed_us),
          (unsigned long)fps_for_window(emu_frames, elapsed_us),
          (unsigned long)fps_for_window(render_frames, elapsed_us),
          (unsigned long)missed_vsyncs,
          (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
          (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
          (unsigned long)average_us(touch_window),
          (unsigned long)touch_window.max_us,
          (unsigned long)average_us(map_window),
          (unsigned long)map_window.max_us,
          (unsigned long)average_us(run_window),
          (unsigned long)run_window.max_us,
          (unsigned long)average_us(draw_window),
          (unsigned long)draw_window.max_us,
          (unsigned long)average_us(flip_window),
          (unsigned long)flip_window.max_us);
      log_window_start = now;
      emu_frames = 0U;
      render_frames = 0U;
      skipped_frames = 0U;
      touch_polls = 0U;
      touch_ok_reads = 0U;
      missed_vsyncs = 0U;
      touch_window = {};
      map_window = {};
      run_window = {};
      draw_window = {};
      flip_window = {};
    }
  }

  vTaskDelete(nullptr);
}

void enter_idle_state(const char *reason, const char *line1, const char *line2) {
  g_idle_reason = reason;
  ESP_LOGW(kTag, "%s", reason);
  present_message_screen(line1, line2, MessageScreen::kError);
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

  if (epd_video_get_backbuffer_size() != kScreenBufferBytes) {
    enter_idle_state("Unexpected EPD backbuffer size.", "EPD SIZE ERR", "CHECK ACTIVE AREA");
    return;
  }

  g_touch_available = touch_init();
  touch_set_rotation(0);
  vc_init();

  if (!allocate_runtime_buffers()) {
    enter_idle_state("Runtime buffer allocation failed.", "BUF ALLOC ERR", "CHECK MEMORY");
    return;
  }

  build_background();

  g_emu = gbemu_create();
  if (g_emu == nullptr) {
    enter_idle_state("Unable to allocate emulator context.", "EMU ALLOC ERR", "CHECK MEMORY");
    return;
  }

  ESP_LOGI(kTag, "ROM source: %s", selected_rom_source());

  const gbemu_status_t init_status = gbemu_init(g_emu, selected_rom_data(), selected_rom_size());
  if (init_status != GBEMU_STATUS_OK) {
    enter_idle_state(
        gbemu_status_string(init_status),
        "ROM INIT ERR",
        gbemu_status_string(init_status));
    gbemu_destroy(g_emu);
    g_emu = nullptr;
    return;
  }

  allocate_quicksave_buffer();

  ESP_LOGI(
      kTag,
      "ROM ready: source=%s title=\"%s\" frame=%ux%u scale=%u rotate=90CW",
      selected_rom_source(),
      gbemu_get_rom_title(g_emu),
      GBEMU_SOURCE_WIDTH,
      GBEMU_SOURCE_HEIGHT,
      GBEMU_SCALE);
  ESP_LOGI(kTag, "touch=%s", g_touch_available ? "ready" : "disabled");

  const BaseType_t rc = xTaskCreatePinnedToCore(
      render_task,
      "gb_touch_render",
      14336,
      nullptr,
      2,
      nullptr,
      0);
  if (rc != pdPASS) {
    enter_idle_state("Failed to create touch render task.", "TASK CREATE ERR", "CHECK MEMORY");
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
