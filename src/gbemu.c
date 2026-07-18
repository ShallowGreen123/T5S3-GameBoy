#include "gbemu.h"

#include <string.h>

#include <esp_heap_caps.h>
#include <esp_timer.h>

#define ENABLE_SOUND 0
#define ENABLE_LCD 1
#define PEANUT_GB_HIGH_LCD_ACCURACY 0
#include "gbcore/peanut_gb.h"

#ifndef GBEMU_FAST_MONO
#define GBEMU_FAST_MONO 0
#endif

struct gbemu_s {
  struct gb_s core;
  const uint8_t *rom_data;
  size_t rom_size;
  uint8_t *cart_ram;
  size_t cart_ram_size;
  uint8_t *framebuffer;
  size_t framebuffer_size;
  uint32_t draw_us_accum;
  uint16_t lines_drawn;
  uint16_t last_error_addr;
  enum gb_error_e last_error;
  gbemu_status_t status;
  bool runtime_error;
  bool render_enabled;
  char rom_title[17];
};

typedef struct gbemu_snapshot_header_s {
  uint32_t magic;
  uint32_t version;
  uint32_t core_size;
  uint32_t cart_ram_size;
} gbemu_snapshot_header_t;

static const uint32_t kSnapshotMagic = 0x47424531UL;
static const uint32_t kSnapshotVersion = 1UL;

static size_t decode_rom_size(uint8_t code) {
  static const size_t kRomSizes[] = {
      32U * 1024U,
      64U * 1024U,
      128U * 1024U,
      256U * 1024U,
      512U * 1024U,
      1024U * 1024U,
      2U * 1024U * 1024U,
      4U * 1024U * 1024U,
      8U * 1024U * 1024U,
  };

  if (code >= (sizeof(kRomSizes) / sizeof(kRomSizes[0]))) {
    return 0;
  }
  return kRomSizes[code];
}

static size_t decode_ram_size(uint8_t code) {
  static const size_t kRamSizes[] = {
      0U,
      2U * 1024U,
      8U * 1024U,
      32U * 1024U,
      128U * 1024U,
      64U * 1024U,
  };

  if (code >= (sizeof(kRamSizes) / sizeof(kRamSizes[0]))) {
    return 0;
  }
  return kRamSizes[code];
}

static inline bool shade_to_white(uint8_t shade, int x, int y) {
#if GBEMU_FAST_MONO
  (void)x;
  (void)y;
  return (shade & 0x03U) <= 1U;
#else
  static const uint8_t kBayer2x2[2][2] = {
      {0U, 2U},
      {3U, 1U},
  };
  const uint8_t rank = kBayer2x2[y & 1][x & 1];

  switch (shade & 0x03U) {
    case 0:
      return true;
    case 1:
      return rank != 3U;
    case 2:
      return rank == 0U;
    default:
      return false;
  }
#endif
}

static void pack_scaled_line(const uint8_t *pixels, uint8_t *framebuffer, uint_fast8_t line) {
  const size_t dest_y_base = (size_t)line * GBEMU_SCALE;

#if GBEMU_FAST_MONO
  uint8_t packed[GBEMU_FRAME_PITCH_BYTES];
  memset(packed, 0x00, sizeof(packed));

  for (uint16_t src_x = 0; src_x < GBEMU_SOURCE_WIDTH; ++src_x) {
    if (!shade_to_white(pixels[src_x], 0, 0)) {
      continue;
    }
    const uint16_t dest_x_base = src_x * GBEMU_SCALE;
    for (uint8_t dx = 0; dx < GBEMU_SCALE; ++dx) {
      const uint16_t dest_x = dest_x_base + dx;
      packed[dest_x >> 3] |= (uint8_t)(0x80U >> (dest_x & 7U));
    }
  }

  for (uint8_t dy = 0; dy < GBEMU_SCALE; ++dy) {
    memcpy(
        framebuffer + ((dest_y_base + dy) * GBEMU_FRAME_PITCH_BYTES),
        packed,
        sizeof(packed));
  }
#else
  uint8_t packed_rows[2][GBEMU_FRAME_PITCH_BYTES];
  memset(packed_rows, 0x00, sizeof(packed_rows));

  for (uint16_t src_x = 0; src_x < GBEMU_SOURCE_WIDTH; ++src_x) {
    const uint8_t shade = pixels[src_x] & 0x03U;
    const uint16_t dest_x_base = src_x * GBEMU_SCALE;
    for (uint8_t parity = 0; parity < 2U; ++parity) {
      for (uint8_t dx = 0; dx < GBEMU_SCALE; ++dx) {
        const uint16_t dest_x = dest_x_base + dx;
        if (shade_to_white(shade, dest_x, parity)) {
          packed_rows[parity][dest_x >> 3] |=
              (uint8_t)(0x80U >> (dest_x & 7U));
        }
      }
    }
  }

  for (uint8_t dy = 0; dy < GBEMU_SCALE; ++dy) {
    const size_t dest_y = dest_y_base + dy;
    memcpy(
        framebuffer + (dest_y * GBEMU_FRAME_PITCH_BYTES),
        packed_rows[dest_y & 1U],
        GBEMU_FRAME_PITCH_BYTES);
  }
#endif
}

static uint8_t gb_rom_read_cb(struct gb_s *gb, const uint_fast32_t addr) {
  const gbemu_t *emu = (const gbemu_t *)gb->direct.priv;
  if (emu == NULL || emu->rom_data == NULL || addr >= emu->rom_size) {
    return 0xFFU;
  }
  return emu->rom_data[addr];
}

static uint8_t gb_cart_ram_read_cb(struct gb_s *gb, const uint_fast32_t addr) {
  const gbemu_t *emu = (const gbemu_t *)gb->direct.priv;
  if (emu == NULL || emu->cart_ram == NULL || emu->cart_ram_size == 0U) {
    return 0xFFU;
  }
  return emu->cart_ram[addr % emu->cart_ram_size];
}

static void gb_cart_ram_write_cb(struct gb_s *gb, const uint_fast32_t addr, const uint8_t value) {
  gbemu_t *emu = (gbemu_t *)gb->direct.priv;
  if (emu == NULL || emu->cart_ram == NULL || emu->cart_ram_size == 0U) {
    return;
  }
  emu->cart_ram[addr % emu->cart_ram_size] = value;
}

static void gb_error_cb(struct gb_s *gb, const enum gb_error_e error, const uint16_t addr) {
  gbemu_t *emu = (gbemu_t *)gb->direct.priv;
  if (emu == NULL) {
    return;
  }

  emu->runtime_error = true;
  emu->status = GBEMU_STATUS_RUNTIME_ERROR;
  emu->last_error = error;
  emu->last_error_addr = addr;
  gb->gb_frame = true;
}

static void gb_lcd_draw_line_cb(struct gb_s *gb, const uint8_t *pixels, const uint_fast8_t line) {
  gbemu_t *emu = (gbemu_t *)gb->direct.priv;
  if (emu == NULL || !emu->render_enabled || emu->framebuffer == NULL || line >= GBEMU_SOURCE_HEIGHT) {
    return;
  }

  const int64_t started_at = esp_timer_get_time();
  pack_scaled_line(pixels, emu->framebuffer, line);

  emu->draw_us_accum += (uint32_t)(esp_timer_get_time() - started_at);
  ++emu->lines_drawn;
}

gbemu_t *gbemu_create(void) {
  gbemu_t *emu = (gbemu_t *)heap_caps_calloc(1, sizeof(gbemu_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (emu == NULL) {
    emu = (gbemu_t *)heap_caps_calloc(1, sizeof(gbemu_t), MALLOC_CAP_8BIT);
  }
  if (emu != NULL) {
    emu->status = GBEMU_STATUS_NO_ROM;
  }
  return emu;
}

void gbemu_destroy(gbemu_t *emu) {
  if (emu == NULL) {
    return;
  }

  if (emu->cart_ram != NULL) {
    heap_caps_free(emu->cart_ram);
  }
  heap_caps_free(emu);
}

gbemu_status_t gbemu_init(gbemu_t *emu, const uint8_t *rom_data, size_t rom_size) {
  enum gb_init_error_e init_error = GB_INIT_NO_ERROR;
  const uint8_t rom_size_code = (rom_size > 0x148U) ? rom_data[0x148] : 0xFFU;
  const uint8_t ram_size_code = (rom_size > 0x149U) ? rom_data[0x149] : 0xFFU;
  size_t expected_rom_size = 0U;

  if (emu == NULL) {
    return GBEMU_STATUS_INVALID_ARGUMENT;
  }

  memset(&emu->core, 0, sizeof(emu->core));
  if (emu->cart_ram != NULL) {
    heap_caps_free(emu->cart_ram);
    emu->cart_ram = NULL;
  }
  emu->cart_ram_size = 0U;
  emu->rom_data = rom_data;
  emu->rom_size = rom_size;
  emu->framebuffer = NULL;
  emu->framebuffer_size = 0U;
  emu->draw_us_accum = 0U;
  emu->lines_drawn = 0U;
  emu->last_error_addr = 0U;
  emu->last_error = GB_UNKNOWN_ERROR;
  emu->runtime_error = false;
  emu->render_enabled = true;
  memset(emu->rom_title, 0, sizeof(emu->rom_title));

  if (rom_data == NULL || rom_size == 0U) {
    emu->status = GBEMU_STATUS_NO_ROM;
    return emu->status;
  }
  if (rom_size < 0x150U) {
    emu->status = GBEMU_STATUS_ROM_TOO_SMALL;
    return emu->status;
  }

  expected_rom_size = decode_rom_size(rom_size_code);
  if (expected_rom_size == 0U) {
    emu->status = GBEMU_STATUS_UNSUPPORTED_ROM_SIZE;
    return emu->status;
  }

  if (ram_size_code > 5U) {
    emu->status = GBEMU_STATUS_UNSUPPORTED_RAM_SIZE;
    return emu->status;
  }

  if (rom_size < expected_rom_size) {
    emu->status = GBEMU_STATUS_ROM_TRUNCATED;
    return emu->status;
  }

  emu->cart_ram_size = decode_ram_size(ram_size_code);
  if (emu->cart_ram_size > 0U) {
    emu->cart_ram = (uint8_t *)heap_caps_calloc(
        1,
        emu->cart_ram_size,
        MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (emu->cart_ram == NULL) {
      emu->cart_ram = (uint8_t *)heap_caps_calloc(1, emu->cart_ram_size, MALLOC_CAP_8BIT);
    }
    if (emu->cart_ram == NULL) {
      emu->status = GBEMU_STATUS_CART_RAM_ALLOC_FAILED;
      return emu->status;
    }
  }

  init_error = gb_init(
      &emu->core,
      gb_rom_read_cb,
      gb_cart_ram_read_cb,
      gb_cart_ram_write_cb,
      gb_error_cb,
      emu);
  if (init_error != GB_INIT_NO_ERROR) {
    emu->status = (init_error == GB_INIT_INVALID_CHECKSUM)
        ? GBEMU_STATUS_INIT_INVALID_CHECKSUM
        : GBEMU_STATUS_INIT_CARTRIDGE_UNSUPPORTED;
    return emu->status;
  }

  gb_init_lcd(&emu->core, gb_lcd_draw_line_cb);
  gb_get_rom_name(&emu->core, emu->rom_title);
  emu->status = GBEMU_STATUS_OK;
  return emu->status;
}

bool gbemu_run_frame(
    gbemu_t *emu,
    uint8_t *framebuffer,
    size_t framebuffer_size,
    uint8_t input_mask,
    bool skip_render,
    gbemu_frame_stats_t *out_stats) {
  const int64_t started_at = esp_timer_get_time();

  if (out_stats != NULL) {
    memset(out_stats, 0, sizeof(*out_stats));
  }

  if (emu == NULL || emu->status != GBEMU_STATUS_OK) {
    return false;
  }
  if (!skip_render && (framebuffer == NULL || framebuffer_size < GBEMU_FRAMEBUFFER_SIZE)) {
    emu->status = GBEMU_STATUS_INVALID_ARGUMENT;
    return false;
  }

  emu->runtime_error = false;
  emu->draw_us_accum = 0U;
  emu->lines_drawn = 0U;
  emu->framebuffer = framebuffer;
  emu->framebuffer_size = framebuffer_size;
  emu->render_enabled = !skip_render;
  emu->core.direct.joypad = (uint8_t)(~input_mask);

  gb_run_frame(&emu->core);

  if (out_stats != NULL) {
    out_stats->run_us = (uint32_t)(esp_timer_get_time() - started_at);
    out_stats->draw_us = emu->draw_us_accum;
    out_stats->lines_drawn = emu->lines_drawn;
    out_stats->rendered = !skip_render;
  }

  return !emu->runtime_error;
}

size_t gbemu_get_state_size(const gbemu_t *emu) {
  if (emu == NULL || emu->status != GBEMU_STATUS_OK) {
    return 0U;
  }
  return sizeof(gbemu_snapshot_header_t) + sizeof(emu->core) + emu->cart_ram_size;
}

bool gbemu_save_state(const gbemu_t *emu, void *buffer, size_t buffer_size) {
  gbemu_snapshot_header_t *header = (gbemu_snapshot_header_t *)buffer;
  uint8_t *payload = NULL;
  const size_t required_size = gbemu_get_state_size(emu);

  if (required_size == 0U || buffer == NULL || buffer_size < required_size) {
    return false;
  }

  header->magic = kSnapshotMagic;
  header->version = kSnapshotVersion;
  header->core_size = (uint32_t)sizeof(emu->core);
  header->cart_ram_size = (uint32_t)emu->cart_ram_size;

  payload = (uint8_t *)(header + 1);
  memcpy(payload, &emu->core, sizeof(emu->core));
  payload += sizeof(emu->core);
  if (emu->cart_ram_size > 0U && emu->cart_ram != NULL) {
    memcpy(payload, emu->cart_ram, emu->cart_ram_size);
  }
  return true;
}

bool gbemu_load_state(gbemu_t *emu, const void *buffer, size_t buffer_size) {
  const gbemu_snapshot_header_t *header = (const gbemu_snapshot_header_t *)buffer;
  const uint8_t *payload = NULL;
  const size_t required_size = gbemu_get_state_size(emu);

  if (required_size == 0U || buffer == NULL || buffer_size < required_size) {
    return false;
  }
  if (header->magic != kSnapshotMagic || header->version != kSnapshotVersion) {
    return false;
  }
  if (header->core_size != sizeof(emu->core) || header->cart_ram_size != emu->cart_ram_size) {
    return false;
  }

  payload = (const uint8_t *)(header + 1);
  memcpy(&emu->core, payload, sizeof(emu->core));
  payload += sizeof(emu->core);
  if (emu->cart_ram_size > 0U && emu->cart_ram != NULL) {
    memcpy(emu->cart_ram, payload, emu->cart_ram_size);
  }

  emu->core.gb_rom_read = gb_rom_read_cb;
  emu->core.gb_cart_ram_read = gb_cart_ram_read_cb;
  emu->core.gb_cart_ram_write = gb_cart_ram_write_cb;
  emu->core.gb_error = gb_error_cb;
  emu->core.direct.priv = emu;
  emu->framebuffer = NULL;
  emu->framebuffer_size = 0U;
  emu->draw_us_accum = 0U;
  emu->lines_drawn = 0U;
  emu->last_error_addr = 0U;
  emu->last_error = GB_UNKNOWN_ERROR;
  emu->runtime_error = false;
  emu->render_enabled = true;
  emu->status = GBEMU_STATUS_OK;
  return true;
}

void gbemu_reset(gbemu_t *emu) {
  if (emu == NULL || emu->status != GBEMU_STATUS_OK) {
    return;
  }

  gb_reset(&emu->core);
  emu->core.gb_rom_read = gb_rom_read_cb;
  emu->core.gb_cart_ram_read = gb_cart_ram_read_cb;
  emu->core.gb_cart_ram_write = gb_cart_ram_write_cb;
  emu->core.gb_error = gb_error_cb;
  emu->core.direct.priv = emu;
  emu->framebuffer = NULL;
  emu->framebuffer_size = 0U;
  emu->draw_us_accum = 0U;
  emu->lines_drawn = 0U;
  emu->last_error_addr = 0U;
  emu->last_error = GB_UNKNOWN_ERROR;
  emu->runtime_error = false;
  emu->render_enabled = true;
  emu->status = GBEMU_STATUS_OK;
}

gbemu_status_t gbemu_get_status(const gbemu_t *emu) {
  return (emu == NULL) ? GBEMU_STATUS_INVALID_ARGUMENT : emu->status;
}

uint16_t gbemu_get_last_error_addr(const gbemu_t *emu) {
  return (emu == NULL) ? 0U : emu->last_error_addr;
}

const char *gbemu_status_string(gbemu_status_t status) {
  switch (status) {
    case GBEMU_STATUS_OK:
      return "ok";
    case GBEMU_STATUS_NO_ROM:
      return "no embedded ROM";
    case GBEMU_STATUS_ROM_TOO_SMALL:
      return "ROM header too small";
    case GBEMU_STATUS_UNSUPPORTED_ROM_SIZE:
      return "unsupported ROM size code";
    case GBEMU_STATUS_UNSUPPORTED_RAM_SIZE:
      return "unsupported RAM size code";
    case GBEMU_STATUS_ROM_TRUNCATED:
      return "ROM image is smaller than its header declares";
    case GBEMU_STATUS_CART_RAM_ALLOC_FAILED:
      return "cart RAM allocation failed";
    case GBEMU_STATUS_INIT_CARTRIDGE_UNSUPPORTED:
      return "unsupported cartridge type";
    case GBEMU_STATUS_INIT_INVALID_CHECKSUM:
      return "invalid ROM header checksum";
    case GBEMU_STATUS_RUNTIME_ERROR:
      return "emulator runtime error";
    default:
      return "invalid argument";
  }
}

const char *gbemu_get_rom_title(const gbemu_t *emu) {
  if (emu == NULL || emu->status != GBEMU_STATUS_OK || emu->rom_title[0] == '\0') {
    return "UNKNOWN";
  }
  return emu->rom_title;
}
