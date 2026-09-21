#include <Arduino.h>
#include <Wire.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_sleep.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <stdio.h>
#include <string.h>

#include "audio.h"
#include "builtin_demo_rom.h"
#include "battery_power.h"
#include "epd_video.h"
#include "gbemu.h"
#include "mono_canvas.h"
#include "night_light.h"
#include "snes_mini_controller.h"
#include "paperboy_config.h"
#include "paperboy_storage.h"
#include "paperboy_ui.h"
#include "paperboy_landscape.h"
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

constexpr const char *kTag = "T5S3-GameBoy";
constexpr const char *kFirmwareVersion = PAPERBOY_FIRMWARE_VERSION;
constexpr uint8_t kMinSkippedFramesBetweenRenders = 1;
constexpr uint8_t kPanelBufferCount = 2;
constexpr uint32_t kDmgClockHz = 4194304U;
constexpr uint32_t kDmgFrameClocks = 70224U;
constexpr uint64_t kGameFramePeriodNumeratorUs =
    static_cast<uint64_t>(kDmgFrameClocks) * 1000000ULL;
constexpr int64_t kGameFramePeriodFloorUs =
    static_cast<int64_t>(kGameFramePeriodNumeratorUs / kDmgClockHz);
constexpr uint32_t kGameFramePeriodRemainder =
    static_cast<uint32_t>(kGameFramePeriodNumeratorUs % kDmgClockHz);
constexpr int64_t kGameFramePeriodCeilingUs =
    kGameFramePeriodFloorUs + (kGameFramePeriodRemainder == 0U ? 0 : 1);
constexpr uint16_t kPanelPitch = t5s3_epd::kActiveWidth / 8U;
constexpr size_t kScreenBytes =
    static_cast<size_t>(kPanelPitch) * t5s3_epd::kActiveHeight;
constexpr size_t kPortraitBytes =
    static_cast<size_t>(PAPERBOY_LOGICAL_PITCH) * PAPERBOY_LOGICAL_HEIGHT;
constexpr uint16_t kGameDirtyY =
    PAPERBOY_LOGICAL_WIDTH - PAPERBOY_GAME_X - GBEMU_FRAME_WIDTH;
constexpr uint16_t kGameDirtyHeight = GBEMU_FRAME_WIDTH;
constexpr uint8_t kClearWhiteFrames = 5;
constexpr uint8_t kClearBlackFrames = 7;
constexpr uint32_t kPowerButtonHoldMs = 2000U;
constexpr uint32_t kBootDebounceMs = 180U;
constexpr uint32_t kShutdownMessageSettleMs = 500U;
constexpr uint32_t kBatteryPollMs = 1000U;
constexpr uint32_t kNoticeDurationMs = 1800U;
constexpr uint8_t kRtcAddress = 0x51U;

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

struct GameFramePacer {
  int64_t next_frame_us = 0;
  uint32_t remainder_phase = 0;
};

Pca9535Min g_expander;
gbemu_t *g_emu = nullptr;
uint8_t *g_background = nullptr;
uint8_t *g_scene = nullptr;
uint8_t *g_game_frame = nullptr;
uint8_t *g_quicksave = nullptr;
size_t g_quicksave_size = 0;
bool g_memory_quicksave_valid = false;
bool g_current_disk_snapshot_available = false;
bool g_persist_write_pending = false;
PaperboyRomData g_sd_rom;
PaperboyStorageConfig g_storage_config;
char g_current_rom_path[PAPERBOY_STORAGE_PATH_MAX] = {0};
char g_notice[48] = {0};
char g_library_status[72] = {0};
uint32_t g_notice_until_ms = 0U;
size_t g_rom_selection = 0U;
bool g_storage_ready = false;
bool g_last_snapshot_available = false;
PaperboyPage g_initial_page = PaperboyPage::Game;
bool g_touch_available = false;
const char *g_idle_reason = nullptr;
volatile bool g_boot_refresh_irq = false;

void IRAM_ATTR on_boot_button_falling() {
  g_boot_refresh_irq = true;
}

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

bool current_rom_is_from_sd() {
  return g_current_rom_path[0] != '\0' && g_sd_rom.data != nullptr;
}

void copy_text(char *destination, size_t destination_size, const char *source) {
  if (destination == nullptr || destination_size == 0U) {
    return;
  }
  snprintf(destination, destination_size, "%s", source == nullptr ? "" : source);
}

void set_notice(const char *message, uint32_t duration_ms = kNoticeDurationMs) {
  copy_text(g_notice, sizeof(g_notice), message);
  g_notice_until_ms = message == nullptr || message[0] == '\0'
      ? 0U
      : millis() + duration_ms;
}

const char *visible_notice() {
  if (g_notice[0] == '\0' || g_notice_until_ms == 0U ||
      static_cast<int32_t>(g_notice_until_ms - millis()) <= 0) {
    return nullptr;
  }
  return g_notice;
}

uint8_t decode_bcd(uint8_t value) {
  return static_cast<uint8_t>(((value >> 4U) * 10U) + (value & 0x0FU));
}

bool leap_year(uint16_t year) {
  return (year % 4U) == 0U && ((year % 100U) != 0U || (year % 400U) == 0U);
}

uint8_t days_in_month(uint16_t year, uint8_t month) {
  static const uint8_t kDays[] = {31U, 28U, 31U, 30U, 31U, 30U,
                                  31U, 31U, 30U, 31U, 30U, 31U};
  if (month == 0U || month > 12U) {
    return 0U;
  }
  return month == 2U && leap_year(year) ? 29U : kDays[month - 1U];
}

uint32_t read_rtc_timestamp() {
  uint8_t registers[7] = {0};
  Wire.beginTransmission(kRtcAddress);
  Wire.write(0x02U);
  if (Wire.endTransmission(false) != 0U ||
      Wire.requestFrom(kRtcAddress, static_cast<uint8_t>(sizeof(registers))) !=
          sizeof(registers)) {
    return 0U;
  }
  for (uint8_t &value : registers) {
    value = static_cast<uint8_t>(Wire.read());
  }

  if ((registers[0] & 0x80U) != 0U) {
    return 0U;
  }
  const uint8_t second = decode_bcd(registers[0] & 0x7FU);
  const uint8_t minute = decode_bcd(registers[1] & 0x7FU);
  const uint8_t hour = decode_bcd(registers[2] & 0x3FU);
  const uint8_t day = decode_bcd(registers[3] & 0x3FU);
  const uint8_t month = decode_bcd(registers[5] & 0x1FU);
  const uint16_t year = static_cast<uint16_t>(2000U + decode_bcd(registers[6]));
  if (second > 59U || minute > 59U || hour > 23U || day == 0U ||
      day > days_in_month(year, month)) {
    return 0U;
  }

  uint32_t days = 0U;
  for (uint16_t calendar_year = 1970U; calendar_year < year; ++calendar_year) {
    days += leap_year(calendar_year) ? 366U : 365U;
  }
  for (uint8_t calendar_month = 1U; calendar_month < month; ++calendar_month) {
    days += days_in_month(year, calendar_month);
  }
  days += static_cast<uint32_t>(day - 1U);
  return days * 86400UL + static_cast<uint32_t>(hour) * 3600UL +
      static_cast<uint32_t>(minute) * 60UL + second;
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

uint16_t bounded_soc(const PaperboyBatteryStatus &battery) {
  return battery.soc_percent > 100U ? 100U : battery.soc_percent;
}

bool battery_indicator_changed(
    const PaperboyBatteryStatus &before,
    const PaperboyBatteryStatus &after) {
  return before.gauge_read_ok != after.gauge_read_ok ||
      before.charger_read_ok != after.charger_read_ok ||
      (after.gauge_read_ok && bounded_soc(before) != bounded_soc(after)) ||
      before.usb_connected != after.usb_connected ||
      before.charging != after.charging ||
      before.charge_done != after.charge_done ||
      before.charge_enabled != after.charge_enabled ||
      before.fault_present != after.fault_present ||
      before.low_battery != after.low_battery;
}

bool battery_is_low(const PaperboyBatteryStatus &battery) {
  return battery.low_battery;
}

void draw_game_low_battery_overlay(
    uint8_t *framebuffer, const PaperboyBatteryStatus &battery) {
  if (framebuffer == nullptr || !battery_is_low(battery)) {
    return;
  }

  mono_fill_rect(
      framebuffer,
      GBEMU_FRAME_PITCH_BYTES,
      GBEMU_FRAME_WIDTH,
      GBEMU_FRAME_HEIGHT,
      0,
      0,
      GBEMU_FRAME_WIDTH,
      38,
      false);
  mono_draw_text(
      framebuffer,
      GBEMU_FRAME_PITCH_BYTES,
      GBEMU_FRAME_WIDTH,
      GBEMU_FRAME_HEIGHT,
      136,
      11,
      "!! LOW BATTERY !!",
      2,
      true);
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

bool find_existing_sidecar(
    const char *rom_path,
    bool state_file,
    char *out_path,
    size_t out_path_size) {
  const bool canonical_path_ok = state_file
      ? paperboy_storage_make_state_path(rom_path, out_path, out_path_size)
      : paperboy_storage_make_save_path(rom_path, out_path, out_path_size);
  if (!canonical_path_ok &&
      paperboy_storage_last_error() != PaperboyStorageError::PathTooLong) {
    return false;
  }
  if (canonical_path_ok && paperboy_storage_file_exists(out_path)) {
    return true;
  }
  if (canonical_path_ok &&
      paperboy_storage_last_error() != PaperboyStorageError::None) {
    return false;
  }

  const bool legacy_path_ok = state_file
      ? paperboy_storage_make_legacy_state_path(rom_path, out_path, out_path_size)
      : paperboy_storage_make_legacy_save_path(rom_path, out_path, out_path_size);
  return legacy_path_ok && paperboy_storage_file_exists(out_path);
}

bool make_sidecar_path_for_write(
    const char *rom_path,
    bool state_file,
    char *out_path,
    size_t out_path_size) {
  const bool canonical_path_ok = state_file
      ? paperboy_storage_make_state_path(rom_path, out_path, out_path_size)
      : paperboy_storage_make_save_path(rom_path, out_path, out_path_size);
  if (canonical_path_ok) {
    return true;
  }
  if (paperboy_storage_last_error() != PaperboyStorageError::PathTooLong) {
    return false;
  }
  return state_file
      ? paperboy_storage_make_legacy_state_path(
            rom_path, out_path, out_path_size)
      : paperboy_storage_make_legacy_save_path(
            rom_path, out_path, out_path_size);
}

void refresh_current_snapshot_availability() {
  g_current_disk_snapshot_available = false;
  if (!current_rom_is_from_sd() || !g_storage_ready) {
    return;
  }

  char state_path[PAPERBOY_STORAGE_PATH_MAX];
  g_current_disk_snapshot_available = find_existing_sidecar(
      g_current_rom_path, true, state_path, sizeof(state_path));
}

void refresh_last_snapshot_availability() {
  g_last_snapshot_available = false;
  if (!g_storage_ready || g_storage_config.last_rom[0] == '\0') {
    return;
  }

  char state_path[PAPERBOY_STORAGE_PATH_MAX];
  if (!paperboy_storage_file_exists(g_storage_config.last_rom) ||
      !find_existing_sidecar(
          g_storage_config.last_rom, true, state_path, sizeof(state_path))) {
    return;
  }
  g_last_snapshot_available = true;
}

void build_rom_library_view(PaperboyRomLibraryView &view) {
  view = {};
  const PaperboyStorageStatus status = paperboy_storage_status();
  view.mounted = status.mounted;
  view.rom_count = static_cast<uint16_t>(status.rom_count);
  if (status.rom_count > 0U) {
    if (g_rom_selection >= status.rom_count) {
      g_rom_selection = status.rom_count - 1U;
    }
    view.selection = static_cast<uint16_t>(g_rom_selection);
    view.first_visible = view.selection < PAPERBOY_ROM_ROWS_VISIBLE
        ? 0U
        : static_cast<uint16_t>(view.selection - PAPERBOY_ROM_ROWS_VISIBLE + 1U);
    for (uint8_t row = 0U; row < PAPERBOY_ROM_ROWS_VISIBLE; ++row) {
      const size_t index = static_cast<size_t>(view.first_visible) + row;
      const PaperboyRomInfo *rom = index < status.rom_count
          ? paperboy_storage_rom(index)
          : nullptr;
      view.visible_names[row] = rom == nullptr ? nullptr : rom->name;
    }
  }
  view.status = g_library_status[0] == '\0' ? nullptr : g_library_status;
  view.audio_engine = audio_engine_name(audio_get_engine());
  view.audio_output_available = audio_output_available();
  view.has_last_snapshot = g_last_snapshot_available;
  const uint64_t card_size_mb = status.card_capacity_bytes / (1024ULL * 1024ULL);
  view.card_size_mb = card_size_mb > UINT32_MAX
      ? UINT32_MAX
      : static_cast<uint32_t>(card_size_mb);
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

  if (paperboy_is_landscape()) { paperboy_landscape_game(game, panel); return; }
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

void reset_game_frame_pacer(GameFramePacer &pacer) {
  pacer.next_frame_us = esp_timer_get_time();
  pacer.remainder_phase = 0U;
}

void pace_game_frame(GameFramePacer &pacer) {
  pacer.next_frame_us += kGameFramePeriodFloorUs;
  pacer.remainder_phase += kGameFramePeriodRemainder;
  if (pacer.remainder_phase >= kDmgClockHz) {
    pacer.remainder_phase -= kDmgClockHz;
    ++pacer.next_frame_us;
  }

  while (true) {
    const int64_t now = esp_timer_get_time();
    const int64_t remaining_us = pacer.next_frame_us - now;
    if (remaining_us <= 0) {
      if (-remaining_us > (kGameFramePeriodCeilingUs * 4)) {
        reset_game_frame_pacer(pacer);
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
    const PaperboyBatteryStatus *battery) {
  if (framebuffer == nullptr || g_background == nullptr ||
      g_scene == nullptr || g_game_frame == nullptr) {
    return;
  }

  if (page == PaperboyPage::Game && paperboy_is_landscape()) {
    paperboy_landscape_draw(g_scene, framebuffer, g_game_frame, buttons, power_on,
        g_memory_quicksave_valid || g_current_disk_snapshot_available, battery, visible_notice());
    return;
  }
  if (page == PaperboyPage::Game) {
    memcpy(g_scene, g_background, kPortraitBytes);
    if (power_on) {
      blit_game_frame(g_scene);
    } else {
      draw_power_off_screen(g_scene);
    }
    paperboy_ui_draw_dynamic(
        g_scene,
        buttons,
        power_on,
        g_memory_quicksave_valid || g_current_disk_snapshot_available,
        battery,
        visible_notice());
  } else {
    PaperboyRomLibraryView library;
    build_rom_library_view(library);
    paperboy_ui_draw_page(
        g_scene,
        page,
        battery,
        kFirmwareVersion,
        g_emu == nullptr ? nullptr : gbemu_get_rom_title(g_emu),
        g_touch_available,
        &library);
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
  paperboy_ui_draw_static(g_background, kFirmwareVersion);
  return true;
}

void release_quicksave() {
  if (g_quicksave != nullptr) {
    heap_caps_free(g_quicksave);
  }
  g_quicksave = nullptr;
  g_quicksave_size = 0U;
  g_memory_quicksave_valid = false;
}

void prepare_quicksave() {
  release_quicksave();
  refresh_current_snapshot_availability();
  g_quicksave_size = gbemu_get_state_size(g_emu);
  if (g_quicksave_size == 0U) {
    ESP_LOGW(kTag, "emulator state snapshots are unavailable");
    return;
  }
  g_quicksave = allocate_buffer(g_quicksave_size, false);
  if (g_quicksave == nullptr) {
    ESP_LOGW(kTag, "quick-save allocation failed (%u bytes)", (unsigned)g_quicksave_size);
    g_quicksave_size = 0U;
    return;
  }

  ESP_LOGI(kTag, "quick-save buffer ready: %u bytes", (unsigned)g_quicksave_size);
}

bool capture_current_quicksave() {
  const size_t required_size = gbemu_get_state_size(g_emu);
  if (required_size == 0U) {
    return false;
  }

  if (g_quicksave == nullptr || g_quicksave_size != required_size) {
    uint8_t *replacement = allocate_buffer(required_size, false);
    if (replacement == nullptr) {
      return false;
    }
    if (g_quicksave != nullptr) {
      heap_caps_free(g_quicksave);
    }
    g_quicksave = replacement;
    g_quicksave_size = required_size;
  }

  g_memory_quicksave_valid = false;
  if (!gbemu_save_state(g_emu, g_quicksave, g_quicksave_size)) {
    return false;
  }
  g_memory_quicksave_valid = true;
  return true;
}

bool read_blob_allocated(const char *path, uint8_t *&data, size_t &size) {
  data = nullptr;
  size = 0U;
  if (!paperboy_storage_file_size(path, size) || size == 0U) {
    return false;
  }
  data = allocate_buffer(size, false);
  if (data == nullptr) {
    ESP_LOGE(kTag, "blob allocation failed path=%s size=%u", path, (unsigned)size);
    size = 0U;
    return false;
  }

  size_t read_size = 0U;
  if (!paperboy_storage_read_blob(path, data, size, read_size) || read_size != size) {
    ESP_LOGE(kTag, "blob read failed path=%s", path);
    heap_caps_free(data);
    data = nullptr;
    size = 0U;
    return false;
  }
  return true;
}

bool save_current_persist(bool only_if_dirty) {
  if (!current_rom_is_from_sd() || !gbemu_has_persist(g_emu)) {
    g_persist_write_pending = false;
    return true;
  }
  if (only_if_dirty && !g_persist_write_pending &&
      !gbemu_persist_is_dirty(g_emu)) {
    return true;
  }
  if (!g_storage_ready) {
    ESP_LOGE(kTag, "cannot save cartridge data: SD card is not mounted");
    return false;
  }

  char save_path[PAPERBOY_STORAGE_PATH_MAX];
  const size_t persist_size = gbemu_get_persist_size(g_emu);
  if (persist_size == 0U || !make_sidecar_path_for_write(
          g_current_rom_path, false, save_path, sizeof(save_path))) {
    return false;
  }

  uint8_t *persist_data = allocate_buffer(persist_size, false);
  if (persist_data == nullptr) {
    ESP_LOGE(kTag, "persist allocation failed (%u bytes)", (unsigned)persist_size);
    return false;
  }
  const uint32_t timestamp = read_rtc_timestamp();
  const bool exported = gbemu_export_persist(
      g_emu, persist_data, persist_size, timestamp);
  const bool written = exported && paperboy_storage_write_blob_atomic(
      save_path, persist_data, persist_size);
  heap_caps_free(persist_data);
  if (!written) {
    ESP_LOGE(
        kTag, "persist save failed path=%s error=%s", save_path,
        paperboy_storage_error_string(paperboy_storage_last_error()));
    return false;
  }

  gbemu_mark_persist_clean(g_emu);
  g_persist_write_pending = false;
  ESP_LOGI(
      kTag, "persist saved path=%s size=%u rtc=%lu", save_path,
      (unsigned)persist_size, (unsigned long)timestamp);
  return true;
}

bool load_persist_into(gbemu_t *emu, const char *rom_path) {
  if (emu == nullptr || !gbemu_has_persist(emu)) {
    return true;
  }

  char save_path[PAPERBOY_STORAGE_PATH_MAX];
  if (!find_existing_sidecar(rom_path, false, save_path, sizeof(save_path))) {
    return paperboy_storage_last_error() == PaperboyStorageError::None;
  }

  uint8_t *persist_data = nullptr;
  size_t persist_size = 0U;
  if (!read_blob_allocated(save_path, persist_data, persist_size)) {
    return false;
  }
  const bool loaded = gbemu_import_persist(
      emu, persist_data, persist_size, read_rtc_timestamp());
  heap_caps_free(persist_data);
  if (loaded) {
    ESP_LOGI(kTag, "persist restored path=%s size=%u", save_path, (unsigned)persist_size);
  } else {
    ESP_LOGW(kTag, "persist rejected path=%s size=%u", save_path, (unsigned)persist_size);
  }
  return loaded;
}

bool load_state_into(gbemu_t *emu, const char *rom_path) {
  char state_path[PAPERBOY_STORAGE_PATH_MAX];
  if (emu == nullptr || !find_existing_sidecar(
          rom_path, true, state_path, sizeof(state_path))) {
    return false;
  }

  uint8_t *state_data = nullptr;
  size_t state_size = 0U;
  if (!read_blob_allocated(state_path, state_data, state_size)) {
    return false;
  }
  const bool loaded = gbemu_load_state(emu, state_data, state_size);
  heap_caps_free(state_data);
  if (loaded) {
    ESP_LOGI(kTag, "snapshot restored path=%s size=%u", state_path, (unsigned)state_size);
  } else {
    ESP_LOGW(kTag, "snapshot rejected path=%s size=%u", state_path, (unsigned)state_size);
  }
  return loaded;
}

bool write_current_config() {
  if (!g_storage_ready) {
    return false;
  }
  g_storage_config.audio_engine = static_cast<uint8_t>(audio_get_engine());
  if (current_rom_is_from_sd()) {
    copy_text(
        g_storage_config.last_rom,
        sizeof(g_storage_config.last_rom),
        g_current_rom_path);
  }
  const bool written = paperboy_storage_write_config(g_storage_config);
  if (!written) {
    ESP_LOGW(
        kTag, "config save failed: %s",
        paperboy_storage_error_string(paperboy_storage_last_error()));
  }
  refresh_last_snapshot_availability();
  return written;
}

bool save_current_session() {
  if (!save_current_persist(false)) {
    return false;
  }
  if (!capture_current_quicksave()) {
    return false;
  }

  bool state_saved = true;
  bool config_saved = true;
  if (current_rom_is_from_sd()) {
    char state_path[PAPERBOY_STORAGE_PATH_MAX];
    state_saved = make_sidecar_path_for_write(
        g_current_rom_path, true, state_path, sizeof(state_path)) &&
        paperboy_storage_write_blob_atomic(
            state_path, g_quicksave, g_quicksave_size);
    if (state_saved) {
      g_current_disk_snapshot_available = true;
      ESP_LOGI(
          kTag, "snapshot saved path=%s size=%u", state_path,
          (unsigned)g_quicksave_size);
      config_saved = write_current_config();
    }
  }
  refresh_last_snapshot_availability();
  return state_saved && config_saved;
}

bool load_current_state() {
  bool loaded = false;
  if (g_memory_quicksave_valid && g_quicksave != nullptr) {
    loaded = gbemu_load_state(g_emu, g_quicksave, g_quicksave_size);
    if (!loaded) {
      g_memory_quicksave_valid = false;
    }
  }
  if (!loaded && current_rom_is_from_sd() && g_storage_ready &&
      g_current_disk_snapshot_available) {
    loaded = load_state_into(g_emu, g_current_rom_path);
    if (!loaded) {
      g_current_disk_snapshot_available = false;
    }
  }
  if (loaded) {
    memset(g_game_frame, 0xFF, GBEMU_FRAMEBUFFER_SIZE);
    if (!capture_current_quicksave()) {
      ESP_LOGW(kTag, "restored state could not be retained in memory");
    }
    if (current_rom_is_from_sd() && gbemu_has_persist(g_emu)) {
      g_persist_write_pending = true;
      if (!save_current_persist(false)) {
        ESP_LOGW(kTag, "restored cartridge data remains pending for a later retry");
      }
    }
  }
  return loaded;
}

void apply_audio_engine(audio_engine_t engine) {
  if (engine < AUDIO_ENGINE_PCM || engine >= AUDIO_ENGINE_COUNT ||
      engine == audio_get_engine()) {
    return;
  }
  audio_deinit();
  audio_set_engine(engine);
  audio_init();
}

void release_sd_candidate(PaperboyRomData &rom, gbemu_t *&emu) {
  if (emu != nullptr) {
    gbemu_destroy(emu);
    emu = nullptr;
  }
  paperboy_storage_free_rom(rom);
}

bool prepare_sd_candidate(
    const char *rom_path,
    bool restore_snapshot,
    PaperboyRomData &candidate_rom,
    gbemu_t *&candidate_emu,
    bool &persist_loaded,
    bool &rom_allocation_failed) {
  candidate_rom = {};
  candidate_emu = nullptr;
  persist_loaded = true;
  rom_allocation_failed = false;

  if (!paperboy_storage_load_rom(rom_path, candidate_rom)) {
    const PaperboyStorageError error = paperboy_storage_last_error();
    rom_allocation_failed = error == PaperboyStorageError::AllocationFailed;
    snprintf(
        g_library_status, sizeof(g_library_status), "ROM LOAD FAILED: %s",
        paperboy_storage_error_string(error));
    return false;
  }

  candidate_emu = gbemu_create();
  if (candidate_emu == nullptr) {
    copy_text(g_library_status, sizeof(g_library_status), "EMULATOR MEMORY ERROR");
    return false;
  }

  const gbemu_status_t init_status = gbemu_init(
      candidate_emu, candidate_rom.data, candidate_rom.size);
  if (init_status != GBEMU_STATUS_OK) {
    snprintf(
        g_library_status, sizeof(g_library_status), "ROM REJECTED: %s",
        gbemu_status_string(init_status));
    return false;
  }

  // Load the durable cartridge data first. A requested full snapshot must be
  // applied last so its SRAM/RTC remains consistent with its CPU and RAM.
  persist_loaded = load_persist_into(candidate_emu, rom_path);
  if (restore_snapshot && !load_state_into(candidate_emu, rom_path)) {
    copy_text(g_library_status, sizeof(g_library_status), "SNAPSHOT NOT AVAILABLE");
    return false;
  }
  return true;
}

bool capture_current_state_for_rom_swap(
    bool &previous_disk_snapshot_available,
    bool preserve_memory_snapshot) {
  previous_disk_snapshot_available = g_current_disk_snapshot_available;
  if (preserve_memory_snapshot && g_memory_quicksave_valid &&
      g_quicksave != nullptr) {
    return true;
  }
  return capture_current_quicksave();
}

bool restore_swapped_out_sd_rom(
    const char *rom_path, bool previous_disk_snapshot_available) {
  PaperboyRomData restored_rom;
  gbemu_t *restored_emu = nullptr;

  if (!paperboy_storage_load_rom(rom_path, restored_rom)) {
    ESP_LOGE(
        kTag, "failed to reload previous ROM path=%s error=%s", rom_path,
        paperboy_storage_error_string(paperboy_storage_last_error()));
    return false;
  }

  restored_emu = gbemu_create();
  if (restored_emu == nullptr) {
    ESP_LOGE(kTag, "failed to recreate emulator for previous ROM");
    paperboy_storage_free_rom(restored_rom);
    return false;
  }

  const gbemu_status_t init_status = gbemu_init(
      restored_emu, restored_rom.data, restored_rom.size);
  if (init_status != GBEMU_STATUS_OK || g_quicksave == nullptr ||
      !gbemu_load_state(restored_emu, g_quicksave, g_quicksave_size)) {
    ESP_LOGE(
        kTag, "failed to restore previous ROM state path=%s init=%s", rom_path,
        gbemu_status_string(init_status));
    gbemu_destroy(restored_emu);
    paperboy_storage_free_rom(restored_rom);
    return false;
  }

  g_emu = restored_emu;
  g_sd_rom = restored_rom;
  copy_text(g_current_rom_path, sizeof(g_current_rom_path), rom_path);
  g_memory_quicksave_valid = true;
  g_current_disk_snapshot_available = previous_disk_snapshot_available;
  g_persist_write_pending = false;
  ESP_LOGW(kTag, "restored previous ROM after failed low-memory switch: %s", rom_path);
  return true;
}

bool activate_builtin_after_failed_rom_swap() {
  gbemu_t *fallback_emu = gbemu_create();
  if (fallback_emu == nullptr) {
    return false;
  }

  const gbemu_status_t init_status = gbemu_init(
      fallback_emu, rom_data(), rom_size());
  if (init_status != GBEMU_STATUS_OK) {
    ESP_LOGE(
        kTag, "built-in ROM recovery failed: %s", gbemu_status_string(init_status));
    gbemu_destroy(fallback_emu);
    return false;
  }

  g_emu = fallback_emu;
  g_sd_rom = {};
  g_current_rom_path[0] = '\0';
  g_persist_write_pending = false;
  memset(g_game_frame, 0xFF, GBEMU_FRAMEBUFFER_SIZE);
  prepare_quicksave();
  ESP_LOGW(kTag, "using built-in ROM because the previous SD ROM could not be restored");
  return true;
}

bool launch_sd_rom(
    const char *rom_path,
    bool restore_snapshot,
    bool preserve_memory_snapshot) {
  if (!g_storage_ready || rom_path == nullptr || rom_path[0] == '\0') {
    copy_text(g_library_status, sizeof(g_library_status), "SD ROM UNAVAILABLE");
    return false;
  }

  char target_rom_path[PAPERBOY_STORAGE_PATH_MAX];
  const int target_path_length = snprintf(
      target_rom_path, sizeof(target_rom_path), "%s", rom_path);
  if (target_path_length <= 0 ||
      static_cast<size_t>(target_path_length) >= sizeof(target_rom_path)) {
    copy_text(g_library_status, sizeof(g_library_status), "ROM PATH TOO LONG");
    return false;
  }
  if (!save_current_persist(true)) {
    copy_text(g_library_status, sizeof(g_library_status), "CURRENT SAVE FAILED");
    return false;
  }

  const size_t previous_apu_size = audio_apu_state_size();
  uint8_t *previous_apu = allocate_buffer(previous_apu_size, true);
  if (previous_apu == nullptr ||
      !audio_apu_state_export(previous_apu, previous_apu_size)) {
    if (previous_apu != nullptr) {
      heap_caps_free(previous_apu);
    }
    copy_text(g_library_status, sizeof(g_library_status), "AUDIO STATE MEMORY ERROR");
    return false;
  }

  PaperboyRomData candidate_rom;
  gbemu_t *candidate_emu = nullptr;
  bool persist_loaded = true;
  bool rom_allocation_failed = false;
  bool candidate_ready = prepare_sd_candidate(
      target_rom_path, restore_snapshot, candidate_rom, candidate_emu,
      persist_loaded, rom_allocation_failed);

  if (!candidate_ready && rom_allocation_failed && current_rom_is_from_sd()) {
    release_sd_candidate(candidate_rom, candidate_emu);

    char previous_rom_path[PAPERBOY_STORAGE_PATH_MAX];
    copy_text(previous_rom_path, sizeof(previous_rom_path), g_current_rom_path);
    bool previous_disk_snapshot_available = false;
    if (!capture_current_state_for_rom_swap(
            previous_disk_snapshot_available,
            preserve_memory_snapshot)) {
      copy_text(g_library_status, sizeof(g_library_status), "ROM SWITCH MEMORY ERROR");
      heap_caps_free(previous_apu);
      return false;
    }

    ESP_LOGW(
        kTag, "ROM allocation failed; unloading %s for low-memory retry",
        previous_rom_path);
    gbemu_destroy(g_emu);
    g_emu = nullptr;
    paperboy_storage_free_rom(g_sd_rom);
    g_current_rom_path[0] = '\0';

    candidate_ready = prepare_sd_candidate(
        target_rom_path, restore_snapshot, candidate_rom, candidate_emu,
        persist_loaded, rom_allocation_failed);
    if (!candidate_ready) {
      char candidate_failure[sizeof(g_library_status)];
      copy_text(candidate_failure, sizeof(candidate_failure), g_library_status);
      release_sd_candidate(candidate_rom, candidate_emu);

      if (restore_swapped_out_sd_rom(
              previous_rom_path, previous_disk_snapshot_available)) {
        copy_text(g_library_status, sizeof(g_library_status), candidate_failure);
        set_notice("ROM SWITCH FAILED");
      } else if (activate_builtin_after_failed_rom_swap()) {
        copy_text(
            g_library_status, sizeof(g_library_status),
            "SWITCH FAILED - BUILTIN ACTIVE");
        set_notice("SD ROM RECOVERY FAILED");
      } else {
        copy_text(g_library_status, sizeof(g_library_status), "ROM RECOVERY FAILED");
        ESP_LOGE(kTag, "no emulator remained after failed ROM switch recovery");
      }
      heap_caps_free(previous_apu);
      return false;
    }
  } else if (!candidate_ready) {
    if (!audio_apu_state_import(previous_apu, previous_apu_size)) {
      ESP_LOGE(kTag, "failed to restore APU after rejected ROM candidate");
    }
    heap_caps_free(previous_apu);
    release_sd_candidate(candidate_rom, candidate_emu);
    return false;
  }

  heap_caps_free(previous_apu);

  gbemu_destroy(g_emu);
  paperboy_storage_free_rom(g_sd_rom);
  g_emu = candidate_emu;
  g_sd_rom = candidate_rom;
  copy_text(g_current_rom_path, sizeof(g_current_rom_path), target_rom_path);
  g_persist_write_pending = restore_snapshot && gbemu_has_persist(g_emu);
  memset(g_game_frame, 0xFF, GBEMU_FRAMEBUFFER_SIZE);
  prepare_quicksave();
  const bool snapshot_persist_saved =
      !g_persist_write_pending || save_current_persist(false);
  (void)write_current_config();

  const char *persist_suffix = "";
  if (!snapshot_persist_saved) {
    persist_suffix = " - SAVE PENDING";
  } else if (!persist_loaded) {
    persist_suffix = " - SAVE REPAIRED";
  }
  snprintf(
      g_library_status, sizeof(g_library_status), "%s%s",
      restore_snapshot ? "SNAPSHOT RESTORED" : "ROM LOADED",
      persist_suffix);
  set_notice(restore_snapshot ? "STATE RESTORED" : "ROM LOADED");
  ESP_LOGI(
      kTag, "launched SD ROM path=%s title=\"%s\" bytes=%u psram=%u",
      g_current_rom_path, gbemu_get_rom_title(g_emu), (unsigned)g_sd_rom.size,
      g_sd_rom.in_psram ? 1U : 0U);
  return true;
}

bool rescan_storage() {
  const bool saved_before_rescan = save_current_persist(true);
  if (!saved_before_rescan) {
    ESP_LOGW(kTag, "cartridge save failed before rescan; retrying after remount");
  }
  paperboy_storage_end();
  const bool scan_ok = paperboy_storage_begin();
  const PaperboyStorageStatus status = paperboy_storage_status();
  g_storage_ready = status.mounted;
  const bool save_recovered = saved_before_rescan ||
      (g_storage_ready && save_current_persist(true));
  g_rom_selection = 0U;

  PaperboyStorageConfig loaded_config;
  paperboy_storage_default_config(loaded_config);
  bool config_ok = true;
  if (g_storage_ready) {
    config_ok = paperboy_storage_read_config(loaded_config);
    if (!config_ok) {
      paperboy_storage_default_config(loaded_config);
    }
    g_storage_config = loaded_config;
    apply_audio_engine(static_cast<audio_engine_t>(g_storage_config.audio_engine));
  } else {
    paperboy_storage_default_config(g_storage_config);
  }
  refresh_current_snapshot_availability();
  refresh_last_snapshot_availability();

  if (!g_storage_ready) {
    copy_text(
        g_library_status,
        sizeof(g_library_status),
        save_recovered ? "SD MOUNT FAILED" : "SD MOUNT FAILED - SAVE PENDING");
  } else if (!save_recovered) {
    copy_text(g_library_status, sizeof(g_library_status), "SAVE FAILED AFTER REMOUNT");
  } else if (!scan_ok) {
    copy_text(g_library_status, sizeof(g_library_status), "ROM SCAN INCOMPLETE");
  } else if (!config_ok) {
    copy_text(g_library_status, sizeof(g_library_status), "CONFIG INVALID - DEFAULTS USED");
  } else {
    snprintf(
        g_library_status, sizeof(g_library_status), "SCAN COMPLETE: %u ROMS",
        static_cast<unsigned>(status.rom_count));
  }
  return g_storage_ready && scan_ok && save_recovered;
}

void on_shutdown() {
  night_light_shutdown();
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

void wait_epd_idle() {
  while (epd_video_submit_pending()) {
    vTaskDelay(1);
  }
}

void clean_panel_white_black_white(const char *reason) {
  wait_epd_idle();
  ESP_LOGI(kTag, "%s refresh: white -> black -> white", reason);
  submit_clear_frame(true, kClearWhiteFrames);
  submit_clear_frame(false, kClearBlackFrames);
  submit_clear_frame(true, kClearWhiteFrames);
  wait_epd_idle();
}

void refresh_current_page(
    PaperboyPage page,
    bool power_on,
    const PaperboyBatteryStatus *battery) {
  clean_panel_white_black_white("BOOT");
  ESP_LOGI(kTag, "BOOT refresh: redrawing page=%u", static_cast<unsigned>(page));

  for (uint8_t copy = 0; copy < kPanelBufferCount; ++copy) {
    wait_epd_idle();
    uint8_t *backbuffer = epd_video_get_backbuffer();
    compose_scene(backbuffer, 0, power_on, page, battery);
    while (!epd_video_submit(0, t5s3_epd::kActiveHeight)) {
      vTaskDelay(1);
    }
  }
  wait_epd_idle();
  ESP_LOGI(kTag, "BOOT refresh: complete");
}

void present_shutdown_page() {
  clean_panel_white_black_white("shutdown");

  ESP_LOGI(kTag, "shutdown refresh: presenting power-off message");
  draw_shutdown_page();
  for (uint8_t copy = 0; copy < kPanelBufferCount; ++copy) {
    wait_epd_idle();
    uint8_t *backbuffer = epd_video_get_backbuffer();
    rotate_portrait_to_panel(g_scene, backbuffer);
    while (!epd_video_submit(0, t5s3_epd::kActiveHeight)) {
      vTaskDelay(1);
    }
  }
  wait_epd_idle();
  delay(kShutdownMessageSettleMs);
  ESP_LOGI(kTag, "shutdown refresh: message stable, cutting power");
}

[[noreturn]] void enter_power_off() {
  ESP_LOGI(kTag, "PCA9535 button held for %lu ms; shutting down", (unsigned long)kPowerButtonHoldMs);
  if (!save_current_persist(true)) {
    ESP_LOGW(kTag, "dirty cartridge save could not be written before shutdown");
  }
  present_shutdown_page();
  night_light_shutdown();
  audio_deinit();
  paperboy_storage_end();
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
  pinMode(t5s3_epd::kBootButton, INPUT_PULLUP);
  pinMode(t5s3_epd::kPca9535Int, INPUT_PULLUP);
  const uint64_t wake_mask =
      (1ULL << t5s3_epd::kBootButton) | (1ULL << t5s3_epd::kPca9535Int);
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
  PaperboyPage page = g_initial_page;
  PaperboyBatteryStatus battery = {};
  uint8_t last_buttons = 0;
  bool last_touch_down = false;
  bool last_boot_pressed = digitalRead(t5s3_epd::kBootButton) == LOW;
  bool boot_refresh_armed = !last_boot_pressed;
  uint32_t pca_button_pressed_since_ms = 0;
  uint32_t last_boot_refresh_ms = 0;
  uint32_t last_battery_poll_ms = millis();
  uint8_t full_scene_syncs = 0;
  uint8_t skipped_since_render = 0;
  uint32_t last_vsync = epd_video_get_vsync_count();
  GameFramePacer game_frame_pacer;
  reset_game_frame_pacer(game_frame_pacer);
  uint64_t stats_started = esp_timer_get_time();
  uint32_t emulated_frames = 0;
  uint32_t rendered_frames = 0;
  uint32_t skipped_frames = 0;
  uint32_t missed_vsyncs = 0;
  TimingWindow run_timing = {};
  TimingWindow draw_timing = {};
  TimingWindow compose_timing = {};
  TimingWindow flip_timing = {};
  bool notice_was_visible = visible_notice() != nullptr;
  bool emu_faulted = false;

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
  ESP_LOGI(
      kTag,
      "charger enabled=%u hiz=%u batfet_disabled=%u otg=%u status=%u "
      "fault=0x%02X input=%u/%u mA ichg=%u adc=%u mA vreg=%u mV "
      "vbus=%u mV vsys=%u mV",
      battery.charge_enabled ? 1U : 0U,
      battery.hiz_enabled ? 1U : 0U,
      battery.batfet_disabled ? 1U : 0U,
      battery.otg_enabled ? 1U : 0U,
      battery.charge_status,
      static_cast<unsigned>(
          (battery.watchdog_fault ? 0x80U : 0U) |
          (battery.boost_fault ? 0x40U : 0U) |
          ((battery.charge_fault & 0x03U) << 4U) |
          (battery.battery_fault ? 0x08U : 0U) |
          (battery.ntc_fault & 0x07U)),
      battery.active_input_limit_ma,
      battery.configured_input_limit_ma,
      battery.configured_charge_current_ma,
      battery.charger_adc_current_ma,
      battery.configured_charge_voltage_mv,
      battery.vbus_voltage_mv,
      battery.system_voltage_mv);

  audio_set_paused(page != PaperboyPage::Game);
  uint8_t *initial = epd_video_get_backbuffer();
  compose_scene(initial, 0, power_on, page, &battery);
  epd_video_flip(0, t5s3_epd::kActiveHeight);
  memcpy(epd_video_get_backbuffer(), initial, kScreenBytes);

  while (true) {
    battery_service();
    touch_state_t touch = {};
    const bool touch_ok = g_touch_available && touch_read(&touch);
    if (g_touch_available) {
      touch_debug_dump_once_per_second();
    }

    const uint32_t now_ms = millis();
    const bool notice_is_visible = visible_notice() != nullptr;
    if (notice_was_visible && !notice_is_visible && page == PaperboyPage::Game) {
      full_scene_syncs = kPanelBufferCount;
    }
    notice_was_visible = notice_is_visible;
    if ((now_ms - last_battery_poll_ms) >= kBatteryPollMs) {
      last_battery_poll_ms = now_ms;
      PaperboyBatteryStatus latest_battery = {};
      const bool ok = battery_read_status(latest_battery);
      const bool indicator_changed =
          battery_indicator_changed(battery, latest_battery);
      battery = latest_battery;
      ESP_LOGI(
          kTag,
          "battery poll=%s soc=%u usb=%u charging=%u full=%u enabled=%u "
          "fault=%u current=%d adc=%u",
          ok ? "ok" : "failed",
          battery.soc_percent,
          battery.usb_connected ? 1U : 0U,
          battery.charging ? 1U : 0U,
          battery.charge_done ? 1U : 0U,
          battery.charge_enabled ? 1U : 0U,
          battery.fault_present ? 1U : 0U,
          battery.average_current_ma,
          battery.charger_adc_current_ma);
      if (indicator_changed && page == PaperboyPage::Game) {
        full_scene_syncs = kPanelBufferCount;
      }
    }

    // Poll on every page so held shoulders cannot become a new press on return.
    const uint8_t controller_buttons = snes_mini_controller_buttons();
    const uint8_t controller_actions = snes_mini_controller_take_actions();
    const uint32_t menu_actions = paperboy_ui_map_controller(
        snes_mini_controller_navigation_buttons(), page, now_ms);
    uint8_t buttons = page == PaperboyPage::Game
        ? ((touch_ok ? paperboy_ui_map_buttons(&touch) : 0U) | (paperboy_ui_controller_ready() ? controller_buttons : 0U))
        : 0U;
    uint32_t actions = touch_ok ? paperboy_ui_map_actions(&touch, page) : 0U;
    actions |= menu_actions;
    if (menu_actions != 0U) full_scene_syncs = kPanelBufferCount;
    if (controller_actions & SNES_ACTION_SETTINGS) {
      actions = PAPERBOY_ACTION_SETTINGS;
    }
    if (page == PaperboyPage::Game) {
      if (controller_actions & SNES_ACTION_ROTATE) actions = PAPERBOY_ACTION_ROTATE;
      if (controller_actions & SNES_ACTION_SAVE) actions |= PAPERBOY_ACTION_SAVE;
      if (controller_actions & SNES_ACTION_LOAD) actions |= PAPERBOY_ACTION_LOAD;
    }
    if (controller_actions & (SNES_ACTION_DIM | SNES_ACTION_BRIGHTEN)) {
      const uint8_t level = night_light_brightness();
      const uint8_t target = (controller_actions & SNES_ACTION_BRIGHTEN)
          ? static_cast<uint8_t>(level + 1U)
          : static_cast<uint8_t>(level > 0U ? level - 1U : 0U);
      (void)night_light_set_brightness(target);
    }
    if (buttons != last_buttons) {
      full_scene_syncs = kPanelBufferCount;
    }

    const bool boot_pressed = digitalRead(t5s3_epd::kBootButton) == LOW;
    const bool boot_irq = g_boot_refresh_irq;
    const bool boot_edge = boot_pressed && !last_boot_pressed;
    bool boot_refresh_completed = false;
    if (boot_refresh_armed && (boot_irq || boot_edge) &&
        (millis() - last_boot_refresh_ms) >= kBootDebounceMs) {
      boot_refresh_armed = false;
      g_boot_refresh_irq = false;
      last_boot_refresh_ms = millis();
      ESP_LOGI(kTag, "BOOT pressed: clean full-screen refresh page=%u",
               static_cast<unsigned>(page));
      refresh_current_page(
          page,
          power_on,
          &battery);
      full_scene_syncs = 0U;
      skipped_since_render = 0U;
      reset_game_frame_pacer(game_frame_pacer);
      boot_refresh_completed = true;
      g_boot_refresh_irq = false;
    }
    const bool boot_pressed_after_refresh =
        digitalRead(t5s3_epd::kBootButton) == LOW;
    if (!boot_pressed_after_refresh && !boot_refresh_completed && !boot_refresh_armed) {
      boot_refresh_armed = true;
      g_boot_refresh_irq = false;
    }
    last_boot_pressed = boot_pressed_after_refresh;

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

    if ((actions & PAPERBOY_ACTION_ROTATE) != 0U) {
      paperboy_orientation_cycle();
      paperboy_ui_on_page_changed();
      full_scene_syncs = kPanelBufferCount;
      skipped_since_render = 0U;
      reset_game_frame_pacer(game_frame_pacer);
      ESP_LOGI(kTag, "screen orientation=%u", static_cast<unsigned>(paperboy_orientation()));
      // Discard input collected against the old layout on this frame.
      last_buttons = 0;
      continue;
    }

    if ((actions & PAPERBOY_ACTION_POWER) != 0U) {
      if (emu_faulted) {
        if (gbemu_get_status(g_emu) != GBEMU_STATUS_OK) {
          gbemu_reset(g_emu);
        }
        if (gbemu_get_status(g_emu) == GBEMU_STATUS_OK) {
          emu_faulted = false;
          power_on = true;
          memset(g_game_frame, 0xFF, GBEMU_FRAMEBUFFER_SIZE);
          set_notice("EMULATOR RESET");
          ESP_LOGI(kTag, "emulator resumed after cold reset");
        } else {
          power_on = false;
          set_notice("RESET FAILED");
          ESP_LOGE(kTag, "emulator cold reset failed");
        }
      } else {
        if (power_on && !save_current_persist(true)) {
          set_notice("SAVE FAILED");
        }
        power_on = !power_on;
        ESP_LOGI(kTag, "soft power=%s", power_on ? "on" : "off");
      }
      audio_set_paused(emu_faulted || !power_on);
      full_scene_syncs = kPanelBufferCount;
      skipped_since_render = 0U;
      reset_game_frame_pacer(game_frame_pacer);
    }

    if ((actions & PAPERBOY_ACTION_SAVE) != 0U) {
      if (emu_faulted) {
        set_notice("RESET OR LOAD FIRST");
        ESP_LOGW(kTag, "state save rejected while emulator is faulted");
      } else if (save_current_session()) {
        set_notice(current_rom_is_from_sd() ? "SAVED TO SD" : "STATE SAVED");
        ESP_LOGI(kTag, "game session saved");
      } else {
        set_notice("SAVE FAILED");
        ESP_LOGW(kTag, "game session save failed");
      }
      full_scene_syncs = kPanelBufferCount;
    }

    if ((actions & PAPERBOY_ACTION_LOAD) != 0U) {
      if (load_current_state()) {
        emu_faulted = false;
        power_on = true;
        audio_set_paused(false);
        set_notice("STATE RESTORED");
        ESP_LOGI(kTag, "game state restored");
      } else {
        set_notice("NO VALID STATE");
        ESP_LOGW(kTag, "state load requested without a valid state");
      }
      full_scene_syncs = kPanelBufferCount;
    }

    PaperboyPage next_page = page;
    const PaperboyStorageStatus storage_status = paperboy_storage_status();
    if ((actions & PAPERBOY_ACTION_ROM_PREVIOUS) != 0U &&
        page == PaperboyPage::SdCard && storage_status.rom_count > 0U) {
      g_rom_selection = g_rom_selection == 0U
          ? storage_status.rom_count - 1U
          : g_rom_selection - 1U;
      g_library_status[0] = '\0';
      full_scene_syncs = kPanelBufferCount;
    }
    if ((actions & PAPERBOY_ACTION_ROM_NEXT) != 0U &&
        page == PaperboyPage::SdCard && storage_status.rom_count > 0U) {
      g_rom_selection = (g_rom_selection + 1U) % storage_status.rom_count;
      g_library_status[0] = '\0';
      full_scene_syncs = kPanelBufferCount;
    }
    if ((actions & PAPERBOY_ACTION_AUDIO_ENGINE) != 0U &&
        page == PaperboyPage::SdCard) {
      const audio_engine_t next_engine = static_cast<audio_engine_t>(
          (static_cast<unsigned>(audio_get_engine()) + 1U) % AUDIO_ENGINE_COUNT);
      apply_audio_engine(next_engine);
      g_storage_config.audio_engine = static_cast<uint8_t>(next_engine);
      const bool config_written = !g_storage_ready || write_current_config();
      snprintf(
          g_library_status, sizeof(g_library_status), "SOUND: %s%s",
          audio_engine_name(next_engine), config_written ? "" : " (CONFIG FAILED)");
      full_scene_syncs = kPanelBufferCount;
    }
    if ((actions & PAPERBOY_ACTION_SD_RESCAN) != 0U &&
        page == PaperboyPage::SdCard) {
      (void)rescan_storage();
      full_scene_syncs = kPanelBufferCount;
    }
    if ((actions & PAPERBOY_ACTION_ROM_LAUNCH) != 0U &&
        page == PaperboyPage::SdCard) {
      const PaperboyRomInfo *rom = paperboy_storage_rom(g_rom_selection);
      if (rom != nullptr && launch_sd_rom(rom->path, false, emu_faulted)) {
        emu_faulted = false;
        power_on = true;
        next_page = PaperboyPage::Game;
      } else if (rom == nullptr) {
        copy_text(g_library_status, sizeof(g_library_status), "SELECT A ROM FIRST");
      }
      full_scene_syncs = kPanelBufferCount;
    }
    if ((actions & PAPERBOY_ACTION_LOAD_LAST) != 0U &&
        page == PaperboyPage::SdCard) {
      char last_rom[PAPERBOY_STORAGE_PATH_MAX];
      copy_text(last_rom, sizeof(last_rom), g_storage_config.last_rom);
      if (g_last_snapshot_available &&
          launch_sd_rom(last_rom, true, emu_faulted)) {
        emu_faulted = false;
        power_on = true;
        next_page = PaperboyPage::Game;
      } else if (!g_last_snapshot_available) {
        copy_text(g_library_status, sizeof(g_library_status), "NO LAST SNAPSHOT");
      }
      full_scene_syncs = kPanelBufferCount;
    }
    if ((actions & PAPERBOY_ACTION_SETTINGS) != 0U) {
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
      buttons = 0U;  // Do not inject the menu activation/back key into gameplay.
      audio_set_paused(page != PaperboyPage::Game || !power_on);
      paperboy_ui_on_page_changed();
      full_scene_syncs = kPanelBufferCount;
      skipped_since_render = 0;
      reset_game_frame_pacer(game_frame_pacer);
    }

    bool pca_button_pressed = false;
    const bool pca_button_ok = read_expander_button(pca_button_pressed);
    if (pca_button_ok && pca_button_pressed) {
      if (pca_button_pressed_since_ms == 0U) {
        pca_button_pressed_since_ms = millis();
      } else if ((millis() - pca_button_pressed_since_ms) >= kPowerButtonHoldMs) {
        enter_power_off();
      }
    } else {
      pca_button_pressed_since_ms = 0U;
    }

    if (page == PaperboyPage::Game && power_on && !emu_faulted) {
      const uint32_t vsync_now = epd_video_get_vsync_count();
      const uint32_t vsync_gap = vsync_now - last_vsync;
      if (vsync_gap > 1U) {
        missed_vsyncs += vsync_gap - 1U;
      }
      last_vsync = vsync_now;

      const bool render_due =
          full_scene_syncs > 0U ||
          skipped_since_render >= kMinSkippedFramesBetweenRenders;
      const bool skip_render = !render_due || !epd_video_can_submit();
      gbemu_frame_stats_t frame_stats = {};

      if (!gbemu_run_frame(
              g_emu,
              skip_render ? nullptr : g_game_frame,
              GBEMU_FRAMEBUFFER_SIZE,
              buttons,
              skip_render,
              &frame_stats)) {
        const gbemu_status_t error_status = gbemu_get_status(g_emu);
        const uint16_t error_addr = gbemu_get_last_error_addr(g_emu);
        const char *error_kind = gbemu_get_last_error_string(g_emu);
        ESP_LOGE(
            kTag,
            "emulator paused: %s (%s) at 0x%04X",
            gbemu_status_string(error_status),
            error_kind,
            error_addr);
        power_on = false;
        emu_faulted = true;
        audio_set_paused(true);
        last_buttons = 0U;
        last_touch_down = touch_down;
        skipped_since_render = 0U;
        full_scene_syncs = kPanelBufferCount;
        reset_game_frame_pacer(game_frame_pacer);
        gbemu_reset(g_emu);
        if (gbemu_get_status(g_emu) != GBEMU_STATUS_OK) {
          ESP_LOGE(kTag, "emulator could not be prepared for recovery");
        }
        set_notice("EMU ERROR - TAP ON-OFF", 10000U);
        continue;
      }
      if (!skip_render) {
        draw_game_low_battery_overlay(g_game_frame, battery);
      }
      audio_service_frame();

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
          compose_scene(backbuffer, buttons, power_on, page, &battery);
        } else {
          rotate_game_to_panel(g_game_frame, backbuffer);
        }
        add_sample(
            compose_timing,
            static_cast<uint32_t>(esp_timer_get_time() - compose_started));
        add_sample(draw_timing, frame_stats.draw_us);
        const int64_t flip_started = esp_timer_get_time();
        const bool submitted = epd_video_submit(
            full_scene ? 0 : (paperboy_is_landscape() ? PAPERBOY_LANDSCAPE_GAME_Y : kGameDirtyY),
            full_scene ? t5s3_epd::kActiveHeight : (paperboy_is_landscape() ? GBEMU_FRAME_HEIGHT : kGameDirtyHeight));
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
      pace_game_frame(game_frame_pacer);
    } else if (page == PaperboyPage::Game && full_scene_syncs > 0U && epd_video_can_submit()) {
      uint8_t *backbuffer = epd_video_get_backbuffer();
      compose_scene(backbuffer, buttons, power_on, page, &battery);
      if (epd_video_submit(0, t5s3_epd::kActiveHeight)) {
        --full_scene_syncs;
      }
    } else if (page != PaperboyPage::Game &&
               full_scene_syncs > 0U && epd_video_can_submit()) {
      uint8_t *backbuffer = epd_video_get_backbuffer();
      compose_scene(backbuffer, 0, power_on, page, &battery);
      if (epd_video_submit(0, t5s3_epd::kActiveHeight)) {
        --full_scene_syncs;
      }
    } else {
      reset_game_frame_pacer(game_frame_pacer);
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

  (void)save_current_persist(true);
  audio_deinit();
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
  night_light_init();

  const bool battery_ready = battery_begin();
  ESP_LOGI(kTag, "battery management initialization=%s", battery_ready ? "ready" : "failed");

  g_touch_available = touch_init();
  touch_set_rotation(0);
  pinMode(t5s3_epd::kBootButton, INPUT_PULLUP);
  attachInterrupt(
      digitalPinToInterrupt(t5s3_epd::kBootButton),
      on_boot_button_falling,
      FALLING);
  paperboy_ui_init();
  if (!allocate_runtime()) {
    enter_idle("runtime buffer allocation failed", "MEMORY ERROR", "BUFFER ALLOCATION");
    return;
  }

  paperboy_storage_default_config(g_storage_config);
  const bool storage_scan_ok = paperboy_storage_begin();
  const PaperboyStorageStatus storage_status = paperboy_storage_status();
  g_storage_ready = storage_status.mounted;
  if (g_storage_ready && !paperboy_storage_read_config(g_storage_config)) {
    paperboy_storage_default_config(g_storage_config);
    copy_text(g_library_status, sizeof(g_library_status), "CONFIG INVALID - DEFAULTS USED");
  } else if (g_storage_ready && !storage_scan_ok) {
    copy_text(g_library_status, sizeof(g_library_status), "ROM SCAN INCOMPLETE");
  } else if (!g_storage_ready) {
    copy_text(g_library_status, sizeof(g_library_status), "SD NOT MOUNTED - BUILTIN ROM");
  }
  audio_set_engine(static_cast<audio_engine_t>(g_storage_config.audio_engine));
  audio_init();
  refresh_last_snapshot_availability();
  if (g_storage_ready && storage_status.rom_count > 0U) {
    g_initial_page = PaperboyPage::SdCard;
  }
  const uint32_t rtc_timestamp = read_rtc_timestamp();
  ESP_LOGI(
      kTag, "storage=%s scan=%s roms=%u audio=%s rtc=%s",
      g_storage_ready ? "mounted" : "unavailable",
      storage_scan_ok ? "ok" : "failed",
      static_cast<unsigned>(storage_status.rom_count),
      audio_engine_name(audio_get_engine()),
      rtc_timestamp == 0U ? "unavailable" : "ready");

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
  prepare_quicksave();

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
      "gameboy_console",
      14336,
      nullptr,
      2,
      nullptr,
      0);
  if (task_result != pdPASS) {
    audio_deinit();
    paperboy_storage_end();
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
