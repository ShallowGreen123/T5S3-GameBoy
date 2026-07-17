#include <Arduino.h>
#include <Wire.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_sleep.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <string.h>

#include "builtin_demo_rom.h"
#include "battery_power.h"
#include "epd_video.h"
#include "gbemu.h"
#include "mono_canvas.h"
#include "paperboy_ui.h"
#include "pca9535_min.h"
#include "t5s3_epd_pins.h"
#include "touch_gt911.h"

#if __has_include("rom/test_rom.h")
#include "rom/test_rom.h"
#define PAPERBOY_HAS_CUSTOM_ROM 1
#else
#define PAPERBOY_HAS_CUSTOM_ROM 0
#endif

namespace {

constexpr const char *kTag = "paperboy";
constexpr const char *kFirmwareVersion = "video-v10-settings";
constexpr uint8_t kMinSkippedFramesBetweenRenders = 1;
constexpr uint8_t kPanelBufferCount = 2;
constexpr int64_t kGameFramePeriodUs = 16742;
constexpr uint16_t kPanelPitch = t5s3_epd::kActiveWidth / 8U;
constexpr size_t kScreenBytes =
    static_cast<size_t>(kPanelPitch) * t5s3_epd::kActiveHeight;
constexpr size_t kPortraitBytes =
    static_cast<size_t>(PAPERBOY_LOGICAL_PITCH) * PAPERBOY_LOGICAL_HEIGHT;
constexpr uint16_t kDynamicDirtyY = 20;
constexpr uint16_t kDynamicDirtyHeight = 500;
constexpr uint16_t kGameDirtyY =
    PAPERBOY_LOGICAL_WIDTH - PAPERBOY_GAME_X - GBEMU_FRAME_WIDTH;
constexpr uint16_t kGameDirtyHeight = GBEMU_FRAME_WIDTH;
constexpr uint8_t kClearWhiteFrames = 5;
constexpr uint8_t kClearBlackFrames = 7;
constexpr uint32_t kPowerButtonHoldMs = 2000U;

static_assert(GBEMU_FRAME_WIDTH == 480U, "unexpected GB frame width");
static_assert(GBEMU_FRAME_HEIGHT == 432U, "unexpected GB frame height");
static_assert((PAPERBOY_GAME_X % 8U) == 0U, "game X must be byte aligned");
static_assert(PAPERBOY_LOGICAL_WIDTH == t5s3_epd::kActiveHeight, "portrait width mismatch");
static_assert(PAPERBOY_LOGICAL_HEIGHT == t5s3_epd::kActiveWidth, "portrait height mismatch");

struct TimingWindow {
  uint32_t count = 0;
  uint64_t total_us = 0;
  uint32_t max_us = 0;
};

Pca9535Min g_expander;
gbemu_t *g_emu = nullptr;
uint8_t *g_background = nullptr;
uint8_t *g_scene = nullptr;
uint8_t *g_game_frame = nullptr;
uint8_t *g_quicksave = nullptr;
size_t g_quicksave_size = 0;
bool g_quicksave_valid = false;
bool g_touch_available = false;
const char *g_idle_reason = nullptr;

const uint8_t *rom_data() {
#if PAPERBOY_HAS_CUSTOM_ROM
  return kTestRomData;
#else
  return builtin_demo_rom_data();
#endif
}

size_t rom_size() {
#if PAPERBOY_HAS_CUSTOM_ROM
  return kTestRomSize;
#else
  return builtin_demo_rom_size();
#endif
}

const char *rom_source() {
#if PAPERBOY_HAS_CUSTOM_ROM
  return "rom/test_rom.h";
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
  return window.count == 0U ? 0U : static_cast<uint32_t>(window.total_us / window.count);
}

uint8_t *allocate_buffer(size_t size, bool prefer_internal) {
  uint8_t *buffer = nullptr;
  if (prefer_internal) {
    buffer = static_cast<uint8_t *>(
        heap_caps_malloc(size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    if (buffer == nullptr) {
      buffer = static_cast<uint8_t *>(
          heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    }
  } else {
    buffer = static_cast<uint8_t *>(
        heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (buffer == nullptr) {
      buffer = static_cast<uint8_t *>(
          heap_caps_malloc(size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    }
  }
  return buffer;
}

void blit_game_frame(uint8_t *framebuffer) {
  const size_t dest_x = PAPERBOY_GAME_X / 8U;
  for (uint16_t row = 0; row < GBEMU_FRAME_HEIGHT; ++row) {
    uint8_t *dest = framebuffer +
        (static_cast<size_t>(PAPERBOY_GAME_Y + row) * PAPERBOY_LOGICAL_PITCH) + dest_x;
    const uint8_t *source = g_game_frame +
        (static_cast<size_t>(row) * GBEMU_FRAME_PITCH_BYTES);
    memcpy(dest, source, GBEMU_FRAME_PITCH_BYTES);
  }
}

void draw_power_off_screen(uint8_t *framebuffer) {
  mono_fill_rect(
      framebuffer, PAPERBOY_LOGICAL_PITCH, PAPERBOY_LOGICAL_WIDTH, PAPERBOY_LOGICAL_HEIGHT,
      PAPERBOY_GAME_X, PAPERBOY_GAME_Y, GBEMU_FRAME_WIDTH, GBEMU_FRAME_HEIGHT, true);
  mono_draw_text(
      framebuffer, PAPERBOY_LOGICAL_PITCH, PAPERBOY_LOGICAL_WIDTH, PAPERBOY_LOGICAL_HEIGHT,
      189, 272, "POWER OFF", 3, false);
  mono_draw_text(
      framebuffer, PAPERBOY_LOGICAL_PITCH, PAPERBOY_LOGICAL_WIDTH, PAPERBOY_LOGICAL_HEIGHT,
      198, 318, "TOUCH ON/OFF", 2, false);
}

void rotate_portrait_to_panel(const uint8_t *portrait, uint8_t *panel) {
  if (portrait == nullptr || panel == nullptr) {
    return;
  }

  // The panel is electrically scanned as 960x540 but mounted in portrait.
  // Logical (x, y) maps to panel (y, 539 - x).
  for (uint16_t panel_y = 0; panel_y < t5s3_epd::kActiveHeight; ++panel_y) {
    const uint16_t logical_x =
        static_cast<uint16_t>(PAPERBOY_LOGICAL_WIDTH - 1U - panel_y);
    uint8_t *dest = panel + static_cast<size_t>(panel_y) * kPanelPitch;
    for (uint16_t byte_x = 0; byte_x < kPanelPitch; ++byte_x) {
      const uint16_t logical_y_base = byte_x * 8U;
      uint8_t packed = 0;
      for (uint8_t bit = 0; bit < 8U; ++bit) {
        const uint16_t logical_y = logical_y_base + bit;
        const size_t source_offset =
            static_cast<size_t>(logical_y) * PAPERBOY_LOGICAL_PITCH + (logical_x >> 3);
        const uint8_t source_mask = static_cast<uint8_t>(0x80U >> (logical_x & 7U));
        if ((portrait[source_offset] & source_mask) != 0U) {
          packed |= static_cast<uint8_t>(0x80U >> bit);
        }
      }
      // The raw panel data polarity is the inverse of the logical canvas:
      // zero drives paper white and one drives paper black.
      dest[byte_x] = static_cast<uint8_t>(~packed);
    }
  }
}

void rotate_game_to_panel(const uint8_t *game, uint8_t *panel) {
  if (game == nullptr || panel == nullptr) {
    return;
  }

  const size_t dest_byte_x = PAPERBOY_GAME_Y / 8U;
  for (uint16_t panel_y = kGameDirtyY;
       panel_y < (kGameDirtyY + kGameDirtyHeight);
       ++panel_y) {
    const uint16_t logical_x =
        static_cast<uint16_t>(PAPERBOY_LOGICAL_WIDTH - 1U - panel_y);
    const uint16_t game_x = static_cast<uint16_t>(logical_x - PAPERBOY_GAME_X);
    const size_t source_byte_x = game_x >> 3;
    const uint8_t source_mask = static_cast<uint8_t>(0x80U >> (game_x & 7U));
    uint8_t *dest =
        panel + (static_cast<size_t>(panel_y) * kPanelPitch) + dest_byte_x;

    for (uint16_t byte_y = 0; byte_y < (GBEMU_FRAME_HEIGHT / 8U); ++byte_y) {
      const uint16_t game_y_base = byte_y * 8U;
      uint8_t packed = 0;
      for (uint8_t bit = 0; bit < 8U; ++bit) {
        const size_t source_offset =
            (static_cast<size_t>(game_y_base + bit) * GBEMU_FRAME_PITCH_BYTES) +
            source_byte_x;
        if ((game[source_offset] & source_mask) != 0U) {
          packed |= static_cast<uint8_t>(0x80U >> bit);
        }
      }
      dest[byte_y] = static_cast<uint8_t>(~packed);
    }
  }
}

void pace_game_frame(int64_t &next_frame_us) {
  next_frame_us += kGameFramePeriodUs;
  while (true) {
    const int64_t now = esp_timer_get_time();
    const int64_t remaining_us = next_frame_us - now;
    if (remaining_us <= 0) {
      if (-remaining_us > (kGameFramePeriodUs * 4)) {
        next_frame_us = now;
      }
      return;
    }
    if (remaining_us > 2000) {
      vTaskDelay(1);
    } else {
      delayMicroseconds(static_cast<unsigned int>(remaining_us));
      return;
    }
  }
}

void compose_scene(
    uint8_t *framebuffer,
    uint8_t buttons,
    bool power_on,
    PaperboyPage page,
    const BatteryStatus *battery) {
  if (framebuffer == nullptr || g_background == nullptr ||
      g_scene == nullptr || g_game_frame == nullptr) {
    return;
  }

  if (page == PaperboyPage::Game) {
    memcpy(g_scene, g_background, kPortraitBytes);
    if (power_on) {
      blit_game_frame(g_scene);
    } else {
      draw_power_off_screen(g_scene);
    }
    paperboy_ui_draw_dynamic(g_scene, buttons, power_on, g_quicksave_valid);
  } else {
    paperboy_ui_draw_page(
        g_scene,
        page,
        battery,
        kFirmwareVersion,
        g_emu == nullptr ? nullptr : gbemu_get_rom_title(g_emu),
        g_touch_available);
  }
  rotate_portrait_to_panel(g_scene, framebuffer);
}

void present_error(const char *headline, const char *detail) {
  uint8_t *backbuffer = epd_video_get_backbuffer();
  if (backbuffer == nullptr) {
    return;
  }
  if (g_scene == nullptr) {
    g_scene = allocate_buffer(kPortraitBytes, false);
    if (g_scene == nullptr) {
      return;
    }
  }

  mono_clear(g_scene, kPortraitBytes, true);
  mono_draw_frame(
      g_scene, PAPERBOY_LOGICAL_PITCH, PAPERBOY_LOGICAL_WIDTH, PAPERBOY_LOGICAL_HEIGHT,
      4, 4, 532, 952, 3, false);
  mono_fill_rect(
      g_scene, PAPERBOY_LOGICAL_PITCH, PAPERBOY_LOGICAL_WIDTH, PAPERBOY_LOGICAL_HEIGHT,
      40, 300, 460, 64, false);
  mono_draw_text(
      g_scene, PAPERBOY_LOGICAL_PITCH, PAPERBOY_LOGICAL_WIDTH, PAPERBOY_LOGICAL_HEIGHT,
      70, 320, headline, 3, true);
  mono_draw_frame(
      g_scene, PAPERBOY_LOGICAL_PITCH, PAPERBOY_LOGICAL_WIDTH, PAPERBOY_LOGICAL_HEIGHT,
      40, 300, 460, 250, 3, false);
  mono_draw_text(
      g_scene, PAPERBOY_LOGICAL_PITCH, PAPERBOY_LOGICAL_WIDTH, PAPERBOY_LOGICAL_HEIGHT,
      70, 410, detail, 2, false);
  mono_draw_text(
      g_scene, PAPERBOY_LOGICAL_PITCH, PAPERBOY_LOGICAL_WIDTH, PAPERBOY_LOGICAL_HEIGHT,
      70, 478, "CHECK SERIAL LOG", 1, false);
  rotate_portrait_to_panel(g_scene, backbuffer);
  epd_video_flip(0, t5s3_epd::kActiveHeight);
}

void scan_i2c_bus() {
  char found[128] = {0};
  size_t offset = 0;
  for (uint8_t address = 1; address < 0x7FU; ++address) {
    Wire.beginTransmission(address);
    if (Wire.endTransmission() == 0) {
      const int written = snprintf(
          found + offset,
          sizeof(found) - offset,
          "%s0x%02X",
          offset == 0U ? "" : " ",
          address);
      if (written <= 0 || static_cast<size_t>(written) >= (sizeof(found) - offset)) {
        break;
      }
      offset += static_cast<size_t>(written);
    }
  }
  ESP_LOGI(kTag, "I2C devices: %s", offset == 0U ? "none" : found);
}

bool init_display() {
  Wire.begin(t5s3_epd::kI2cSda, t5s3_epd::kI2cScl);
  Wire.setClock(400000);
  Wire.setTimeout(100);
  scan_i2c_bus();

  if (!g_expander.begin(Wire, t5s3_epd::kPca9535Address) ||
      !g_expander.configureProbeDefaults()) {
    ESP_LOGE(kTag, "PCA9535 initialization failed");
    return false;
  }
  if (!epd_video_init(g_expander) || !epd_video_power_on() || !epd_video_start()) {
    ESP_LOGE(kTag, "EPD video initialization failed");
    epd_video_shutdown();
    return false;
  }
  return true;
}

void wait_vsync_frames(uint8_t frame_count) {
  const uint32_t target = epd_video_get_vsync_count() + frame_count;
  while (static_cast<int32_t>(epd_video_get_vsync_count() - target) < 0) {
    vTaskDelay(1);
  }
}

void submit_clear_frame(bool white, uint8_t hold_frames) {
  uint8_t *backbuffer = epd_video_get_backbuffer();
  if (backbuffer == nullptr) {
    return;
  }
  // This buffer goes directly to the panel and does not pass through the
  // portrait conversion, so apply the physical panel polarity here too.
  memset(backbuffer, white ? 0x00 : 0xFF, kScreenBytes);
  epd_video_flip(0, t5s3_epd::kActiveHeight);
  wait_vsync_frames(hold_frames);
}

void perform_startup_clear() {
  ESP_LOGI(kTag, "startup full clear: white -> black -> white");
  submit_clear_frame(true, kClearWhiteFrames);
  submit_clear_frame(false, kClearBlackFrames);
  submit_clear_frame(true, kClearWhiteFrames);
  ESP_LOGI(kTag, "startup full clear complete");
}

bool allocate_runtime() {
  g_background = allocate_buffer(kPortraitBytes, false);
  g_scene = allocate_buffer(kPortraitBytes, true);
  g_game_frame = allocate_buffer(GBEMU_FRAMEBUFFER_SIZE, true);
  if (g_background == nullptr || g_scene == nullptr || g_game_frame == nullptr) {
    return false;
  }
  memset(g_game_frame, 0xFF, GBEMU_FRAMEBUFFER_SIZE);
  paperboy_ui_draw_static(g_background);
  return true;
}

void allocate_quicksave() {
  g_quicksave_size = gbemu_get_state_size(g_emu);
  if (g_quicksave_size == 0U) {
    ESP_LOGW(kTag, "emulator state snapshots are unavailable");
    return;
  }
  g_quicksave = allocate_buffer(g_quicksave_size, false);
  if (g_quicksave == nullptr) {
    ESP_LOGW(kTag, "quick-save allocation failed (%u bytes)", (unsigned)g_quicksave_size);
    return;
  }
  ESP_LOGI(kTag, "quick-save buffer ready: %u bytes", (unsigned)g_quicksave_size);
}

void on_shutdown() {
  epd_video_shutdown();
}

bool read_expander_button(bool &pressed) {
  uint8_t input0 = 0;
  uint8_t input1 = 0;
  if (!g_expander.readInputs(input0, input1)) {
    pressed = false;
    return false;
  }
  (void)input0;
  pressed = (input1 & t5s3_epd::kPcaMaskButton) == 0U;
  return true;
}

void draw_shutdown_page() {
  mono_clear(g_scene, kPortraitBytes, true);
  mono_draw_frame(
      g_scene, PAPERBOY_LOGICAL_PITCH, PAPERBOY_LOGICAL_WIDTH, PAPERBOY_LOGICAL_HEIGHT,
      4, 4, 532, 952, 3, false);
  mono_draw_text(
      g_scene, PAPERBOY_LOGICAL_PITCH, PAPERBOY_LOGICAL_WIDTH, PAPERBOY_LOGICAL_HEIGHT,
      126, 350, "POWERING OFF", 4, false);
  mono_draw_line(
      g_scene, PAPERBOY_LOGICAL_PITCH, PAPERBOY_LOGICAL_WIDTH, PAPERBOY_LOGICAL_HEIGHT,
      90, 430, 450, 430, false);
  mono_draw_text(
      g_scene, PAPERBOY_LOGICAL_PITCH, PAPERBOY_LOGICAL_WIDTH, PAPERBOY_LOGICAL_HEIGHT,
      150, 480, "PLEASE WAIT", 3, false);
  mono_draw_text(
      g_scene, PAPERBOY_LOGICAL_PITCH, PAPERBOY_LOGICAL_WIDTH, PAPERBOY_LOGICAL_HEIGHT,
      162, 560, "HOLD PWR TO START", 2, false);
}

void present_shutdown_page() {
  while (epd_video_submit_pending()) {
    vTaskDelay(1);
  }
  draw_shutdown_page();
  for (uint8_t copy = 0; copy < kPanelBufferCount; ++copy) {
    while (epd_video_submit_pending()) {
      vTaskDelay(1);
    }
    uint8_t *backbuffer = epd_video_get_backbuffer();
    rotate_portrait_to_panel(g_scene, backbuffer);
    while (!epd_video_submit(0, t5s3_epd::kActiveHeight)) {
      vTaskDelay(1);
    }
  }
  while (epd_video_submit_pending()) {
    vTaskDelay(1);
  }
}

[[noreturn]] void enter_power_off() {
  ESP_LOGI(kTag, "PCA9535 button held for %lu ms; shutting down", (unsigned long)kPowerButtonHoldMs);
  present_shutdown_page();
  epd_video_shutdown();

  const BatteryShutdownResult result = battery_request_shutdown();
  ESP_LOGI(kTag, "BQ25896 shutdown result=%u", static_cast<unsigned>(result));
  if (result == BatteryShutdownResult::PowerCutRequested) {
    delay(1500);
  }

  // BATFET cannot disconnect the system while USB is supplying VBUS. Keep the
  // screen and fall back to deep sleep, with BOOT/PWR and PCA9535 INT as wake
  // sources. Wait for button release first to avoid an immediate wake-up.
  bool pressed = true;
  while (pressed) {
    if (!read_expander_button(pressed)) {
      break;
    }
    delay(20);
  }
  pinMode(0, INPUT_PULLUP);
  pinMode(38, INPUT_PULLUP);
  const uint64_t wake_mask = (1ULL << 0U) | (1ULL << 38U);
  esp_sleep_enable_ext1_wakeup(wake_mask, ESP_EXT1_WAKEUP_ANY_LOW);
  ESP_LOGI(kTag, "entering deep sleep fallback");
  delay(30);
  esp_deep_sleep_start();
  while (true) {
    delay(1000);
  }
}

void run_console(void *unused) {
  (void)unused;

  bool power_on = true;
  PaperboyPage page = PaperboyPage::Game;
  BatteryStatus battery = {};
  uint8_t last_buttons = 0;
  bool last_touch_down = false;
  uint32_t pca_button_pressed_since_ms = 0;
  uint8_t full_scene_syncs = 0;
  uint8_t skipped_since_render = 0;
  uint32_t last_vsync = epd_video_get_vsync_count();
  int64_t next_game_frame_us = esp_timer_get_time();
  uint64_t stats_started = esp_timer_get_time();
  uint32_t emulated_frames = 0;
  uint32_t rendered_frames = 0;
  uint32_t skipped_frames = 0;
  uint32_t missed_vsyncs = 0;
  TimingWindow run_timing = {};
  TimingWindow draw_timing = {};
  TimingWindow compose_timing = {};
  TimingWindow flip_timing = {};

  const bool battery_probe_ok = battery_read_status(battery);
  ESP_LOGI(
      kTag,
      "battery probe=%s gauge=%s charger=%s soc=%u voltage=%u current=%d avg=%d usb=%s",
      battery_probe_ok ? "ok" : "failed",
      battery.gauge_read_ok ? "online" : (battery.gauge_found ? "read-error" : "missing"),
      battery.charger_read_ok ? "online" : (battery.charger_found ? "read-error" : "missing"),
      battery.soc_percent,
      battery.voltage_mv,
      battery.current_ma,
      battery.average_current_ma,
      battery.usb_connected ? "yes" : "no");

  uint8_t *initial = epd_video_get_backbuffer();
  compose_scene(initial, 0, power_on, page, nullptr);
  epd_video_flip(0, t5s3_epd::kActiveHeight);
  memcpy(epd_video_get_backbuffer(), initial, kScreenBytes);

  while (true) {
    touch_state_t touch = {};
    const bool touch_ok = g_touch_available && touch_read(&touch);
    if (g_touch_available) {
      touch_debug_dump_once_per_second();
    }

    const uint8_t buttons =
        (touch_ok && page == PaperboyPage::Game) ? paperboy_ui_map_buttons(&touch) : 0U;
    const uint32_t actions = touch_ok ? paperboy_ui_map_actions(&touch, page) : 0U;
    if (buttons != last_buttons) {
      full_scene_syncs = kPanelBufferCount;
    }

    const bool touch_down = touch_ok && touch.touched && touch.points > 0U;
    if (touch_down && !last_touch_down) {
      ESP_LOGI(
          kTag,
          "touch down points=%u first=%u,%u",
          touch.points,
          touch.x[0],
          touch.y[0]);
    }

    if (buttons != last_buttons) {
      ESP_LOGI(
          kTag,
          "touch buttons=0x%02X points=%u first=%u,%u",
          buttons,
          touch_ok ? touch.points : 0U,
          (touch_ok && touch.points > 0U) ? touch.x[0] : 0U,
          (touch_ok && touch.points > 0U) ? touch.y[0] : 0U);
    }

    if ((actions & PAPERBOY_ACTION_POWER) != 0U) {
      power_on = !power_on;
      full_scene_syncs = kPanelBufferCount;
      ESP_LOGI(kTag, "soft power=%s", power_on ? "on" : "off");
    }

    if ((actions & PAPERBOY_ACTION_SAVE) != 0U) {
      if (g_quicksave != nullptr &&
          gbemu_save_state(g_emu, g_quicksave, g_quicksave_size)) {
        g_quicksave_valid = true;
        ESP_LOGI(kTag, "quick state saved in memory");
      } else {
        ESP_LOGW(kTag, "quick save unavailable");
      }
      full_scene_syncs = kPanelBufferCount;
    }

    if ((actions & PAPERBOY_ACTION_LOAD) != 0U) {
      if (g_quicksave_valid && g_quicksave != nullptr &&
          gbemu_load_state(g_emu, g_quicksave, g_quicksave_size)) {
        power_on = true;
        ESP_LOGI(kTag, "quick state restored");
      } else {
        ESP_LOGW(kTag, "quick load requested without a state");
      }
      full_scene_syncs = kPanelBufferCount;
    }

    PaperboyPage next_page = page;
    if ((actions & PAPERBOY_ACTION_SETTINGS) != 0U && page == PaperboyPage::Game) {
      next_page = PaperboyPage::Settings;
    }
    if ((actions & PAPERBOY_ACTION_BACK) != 0U) {
      next_page = page == PaperboyPage::Settings ? PaperboyPage::Game : PaperboyPage::Settings;
    }
    if ((actions & PAPERBOY_ACTION_HOME) != 0U) {
      next_page = PaperboyPage::Game;
    }
    if ((actions & PAPERBOY_ACTION_BATTERY) != 0U && page == PaperboyPage::Settings) {
      next_page = PaperboyPage::Battery;
    }
    if ((actions & PAPERBOY_ACTION_SD_CARD) != 0U && page == PaperboyPage::Settings) {
      next_page = PaperboyPage::SdCard;
    }
    if ((actions & PAPERBOY_ACTION_ABOUT) != 0U && page == PaperboyPage::Settings) {
      next_page = PaperboyPage::About;
    }
    if ((actions & PAPERBOY_ACTION_REFRESH) != 0U && page == PaperboyPage::Battery) {
      const bool ok = battery_read_status(battery);
      ESP_LOGI(kTag, "battery refresh %s soc=%u voltage=%u", ok ? "ok" : "failed",
               battery.soc_percent, battery.voltage_mv);
      full_scene_syncs = kPanelBufferCount;
    }
    if (next_page != page) {
      if (next_page == PaperboyPage::Battery) {
        const bool ok = battery_read_status(battery);
        ESP_LOGI(kTag, "battery page %s soc=%u voltage=%u", ok ? "ok" : "failed",
                 battery.soc_percent, battery.voltage_mv);
      }
      ESP_LOGI(kTag, "page %u -> %u", static_cast<unsigned>(page), static_cast<unsigned>(next_page));
      page = next_page;
      paperboy_ui_on_page_changed();
      full_scene_syncs = kPanelBufferCount;
      skipped_since_render = 0;
      next_game_frame_us = esp_timer_get_time();
    }

    bool pca_button_pressed = false;
    const bool pca_button_ok = read_expander_button(pca_button_pressed);
    if (page == PaperboyPage::Game && pca_button_ok && pca_button_pressed) {
      if (pca_button_pressed_since_ms == 0U) {
        pca_button_pressed_since_ms = millis();
      } else if ((millis() - pca_button_pressed_since_ms) >= kPowerButtonHoldMs) {
        enter_power_off();
      }
    } else {
      pca_button_pressed_since_ms = 0U;
    }

    if (page == PaperboyPage::Game && power_on) {
      const uint32_t vsync_now = epd_video_get_vsync_count();
      const uint32_t vsync_gap = vsync_now - last_vsync;
      if (vsync_gap > 1U) {
        missed_vsyncs += vsync_gap - 1U;
      }
      last_vsync = vsync_now;

      const bool render_due =
          full_scene_syncs > 0U ||
          skipped_since_render >= kMinSkippedFramesBetweenRenders;
      const bool skip_render = !render_due || epd_video_submit_pending();
      gbemu_frame_stats_t frame_stats = {};

      if (!gbemu_run_frame(
              g_emu,
              skip_render ? nullptr : g_game_frame,
              GBEMU_FRAMEBUFFER_SIZE,
              buttons,
              skip_render,
              &frame_stats)) {
        ESP_LOGE(
            kTag,
            "emulator stopped: %s at 0x%04X",
            gbemu_status_string(gbemu_get_status(g_emu)),
            gbemu_get_last_error_addr(g_emu));
        present_error("EMU ERROR", "RUNTIME FAILURE");
        break;
      }

      add_sample(run_timing, frame_stats.run_us);
      ++emulated_frames;
      if (skip_render) {
        ++skipped_frames;
        if (skipped_since_render < UINT8_MAX) {
          ++skipped_since_render;
        }
      } else {
        uint8_t *backbuffer = epd_video_get_backbuffer();
        const int64_t compose_started = esp_timer_get_time();
        const bool full_scene = full_scene_syncs > 0U;
        if (full_scene) {
          compose_scene(backbuffer, buttons, power_on, page, nullptr);
        } else {
          rotate_game_to_panel(g_game_frame, backbuffer);
        }
        add_sample(
            compose_timing,
            static_cast<uint32_t>(esp_timer_get_time() - compose_started));
        add_sample(draw_timing, frame_stats.draw_us);
        const int64_t flip_started = esp_timer_get_time();
        const bool submitted = epd_video_submit(
            full_scene ? kDynamicDirtyY : kGameDirtyY,
            full_scene ? kDynamicDirtyHeight : kGameDirtyHeight);
        add_sample(flip_timing, static_cast<uint32_t>(esp_timer_get_time() - flip_started));
        if (submitted) {
          ++rendered_frames;
          skipped_since_render = 0;
          if (full_scene_syncs > 0U) {
            --full_scene_syncs;
          }
        } else {
          ++skipped_frames;
          if (skipped_since_render < UINT8_MAX) {
            ++skipped_since_render;
          }
        }
      }
      pace_game_frame(next_game_frame_us);
    } else if (page == PaperboyPage::Game && full_scene_syncs > 0U && !epd_video_submit_pending()) {
      uint8_t *backbuffer = epd_video_get_backbuffer();
      compose_scene(backbuffer, buttons, power_on, page, nullptr);
      if (epd_video_submit(kDynamicDirtyY, kDynamicDirtyHeight)) {
        --full_scene_syncs;
      }
    } else if (page != PaperboyPage::Game &&
               full_scene_syncs > 0U && !epd_video_submit_pending()) {
      uint8_t *backbuffer = epd_video_get_backbuffer();
      compose_scene(backbuffer, 0, power_on, page, &battery);
      if (epd_video_submit(0, t5s3_epd::kActiveHeight)) {
        --full_scene_syncs;
      }
    } else {
      next_game_frame_us = esp_timer_get_time();
      vTaskDelay(pdMS_TO_TICKS(5));
    }

    last_buttons = buttons;
    last_touch_down = touch_down;
    const uint64_t now = esp_timer_get_time();
    if ((now - stats_started) >= 1000000ULL) {
      ESP_LOGI(
          kTag,
          "page=%u power=%s emu=%lu render=%lu skip=%lu missed=%lu run(us avg/max)=%lu/%lu draw=%lu/%lu compose=%lu/%lu submit=%lu/%lu heap=%u psram=%u",
          static_cast<unsigned>(page),
          power_on ? "on" : "off",
          (unsigned long)emulated_frames,
          (unsigned long)rendered_frames,
          (unsigned long)skipped_frames,
          (unsigned long)missed_vsyncs,
          (unsigned long)average_us(run_timing),
          (unsigned long)run_timing.max_us,
          (unsigned long)average_us(draw_timing),
          (unsigned long)draw_timing.max_us,
          (unsigned long)average_us(compose_timing),
          (unsigned long)compose_timing.max_us,
          (unsigned long)average_us(flip_timing),
          (unsigned long)flip_timing.max_us,
          (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
          (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
      stats_started = now;
      emulated_frames = 0;
      rendered_frames = 0;
      skipped_frames = 0;
      missed_vsyncs = 0;
      run_timing = {};
      draw_timing = {};
      compose_timing = {};
      flip_timing = {};
    }
  }

  vTaskDelete(nullptr);
}

void enter_idle(const char *reason, const char *headline, const char *detail) {
  g_idle_reason = reason;
  ESP_LOGE(kTag, "%s", reason);
  present_error(headline, detail);
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(1500);

  ESP_LOGI(kTag, "starting %s firmware=%s", t5s3_epd::kBoardName, kFirmwareVersion);
  ESP_LOGI(
      kTag,
      "memory internal=%u psram=%u",
      (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
      (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

  (void)esp_register_shutdown_handler(on_shutdown);
  if (!init_display()) {
    ESP_LOGE(kTag, "display pipeline unavailable");
    return;
  }
  if (epd_video_get_backbuffer_size() != kScreenBytes) {
    enter_idle("unexpected EPD backbuffer size", "EPD SIZE ERROR", "CHECK PANEL CONFIG");
    return;
  }

  perform_startup_clear();

  g_touch_available = touch_init();
  touch_set_rotation(0);
  paperboy_ui_init();
  if (!allocate_runtime()) {
    enter_idle("runtime buffer allocation failed", "MEMORY ERROR", "BUFFER ALLOCATION");
    return;
  }

  g_emu = gbemu_create();
  if (g_emu == nullptr) {
    enter_idle("emulator allocation failed", "MEMORY ERROR", "EMULATOR CORE");
    return;
  }

  ESP_LOGI(kTag, "ROM source: %s", rom_source());
  const gbemu_status_t init_status = gbemu_init(g_emu, rom_data(), rom_size());
  if (init_status != GBEMU_STATUS_OK) {
    ESP_LOGE(kTag, "ROM init failed: %s", gbemu_status_string(init_status));
    enter_idle("ROM initialization failed", "ROM ERROR", "CHECK ROM HEADER");
    return;
  }
  allocate_quicksave();

  ESP_LOGI(
      kTag,
      "ready title=\"%s\" touch=%s logical=%ux%u game=%ux%u scale=%u portrait",
      gbemu_get_rom_title(g_emu),
      g_touch_available ? "ready" : "missing",
      PAPERBOY_LOGICAL_WIDTH,
      PAPERBOY_LOGICAL_HEIGHT,
      GBEMU_FRAME_WIDTH,
      GBEMU_FRAME_HEIGHT,
      GBEMU_SCALE);

  const BaseType_t task_result = xTaskCreatePinnedToCore(
      run_console,
      "paperboy_console",
      14336,
      nullptr,
      2,
      nullptr,
      0);
  if (task_result != pdPASS) {
    enter_idle("console task creation failed", "TASK ERROR", "CHECK INTERNAL RAM");
  }
}

void loop() {
  static uint32_t last_idle_log_ms = 0;
  if (g_idle_reason != nullptr && (millis() - last_idle_log_ms) >= 5000U) {
    ESP_LOGE(kTag, "%s", g_idle_reason);
    last_idle_log_ms = millis();
  }
  delay(250);
}
