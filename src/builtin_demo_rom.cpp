#include "builtin_demo_rom.h"

#include <string.h>

namespace {

constexpr size_t kDemoRomSize = 32U * 1024U;
constexpr uint16_t kHeaderChecksumStart = 0x0134U;
constexpr uint16_t kHeaderChecksumEnd = 0x014CU;
constexpr uint16_t kGlobalChecksumHigh = 0x014EU;
constexpr uint16_t kGlobalChecksumLow = 0x014FU;
constexpr uint16_t kTileDataAddress = 0x0200U;
constexpr uint16_t kMapDataAddress = 0x0400U;
constexpr uint8_t kNintendoLogo[48] = {
    0xCE, 0xED, 0x66, 0x66, 0xCC, 0x0D, 0x00, 0x0B,
    0x03, 0x73, 0x00, 0x83, 0x00, 0x0C, 0x00, 0x0D,
    0x00, 0x08, 0x11, 0x1F, 0x88, 0x89, 0x00, 0x0E,
    0xDC, 0xCC, 0x6E, 0xE6, 0xDD, 0xDD, 0xD9, 0x99,
    0xBB, 0xBB, 0x67, 0x63, 0x6E, 0x0E, 0xEC, 0xCC,
    0xDD, 0xDC, 0x99, 0x9F, 0xBB, 0xB9, 0x33, 0x3E,
};

// 5x7 uppercase font, stored column-first. Tile 0 is blank and tiles 1-26
// correspond to A-Z. The ROM is generated locally and contains no game data.
constexpr uint8_t kFont[26][5] = {
    {0x7E, 0x09, 0x09, 0x09, 0x7E},  // A
    {0x7F, 0x49, 0x49, 0x49, 0x36},  // B
    {0x3E, 0x41, 0x41, 0x41, 0x22},  // C
    {0x7F, 0x41, 0x41, 0x22, 0x1C},  // D
    {0x7F, 0x49, 0x49, 0x49, 0x41},  // E
    {0x7F, 0x09, 0x09, 0x09, 0x01},  // F
    {0x3E, 0x41, 0x49, 0x49, 0x7A},  // G
    {0x7F, 0x08, 0x08, 0x08, 0x7F},  // H
    {0x00, 0x41, 0x7F, 0x41, 0x00},  // I
    {0x20, 0x40, 0x41, 0x3F, 0x01},  // J
    {0x7F, 0x08, 0x14, 0x22, 0x41},  // K
    {0x7F, 0x40, 0x40, 0x40, 0x40},  // L
    {0x7F, 0x02, 0x0C, 0x02, 0x7F},  // M
    {0x7F, 0x04, 0x08, 0x10, 0x7F},  // N
    {0x3E, 0x41, 0x41, 0x41, 0x3E},  // O
    {0x7F, 0x09, 0x09, 0x09, 0x06},  // P
    {0x3E, 0x41, 0x51, 0x21, 0x5E},  // Q
    {0x7F, 0x09, 0x19, 0x29, 0x46},  // R
    {0x46, 0x49, 0x49, 0x49, 0x31},  // S
    {0x01, 0x01, 0x7F, 0x01, 0x01},  // T
    {0x3F, 0x40, 0x40, 0x40, 0x3F},  // U
    {0x1F, 0x20, 0x40, 0x20, 0x1F},  // V
    {0x7F, 0x20, 0x18, 0x20, 0x7F},  // W
    {0x63, 0x14, 0x08, 0x14, 0x63},  // X
    {0x03, 0x04, 0x78, 0x04, 0x03},  // Y
    {0x61, 0x51, 0x49, 0x45, 0x43},  // Z
};

// Disable LCD, copy 32 tiles and one complete BG map into VRAM, then enable
// the LCD and wait frame-by-frame. This is a valid ROM executed by Peanut-GB,
// not a framebuffer shortcut.
constexpr uint8_t kProgram[] = {
    0xF3,                         // DI
    0x31, 0xFE, 0xFF,             // LD SP,$FFFE
    0xF0, 0x44, 0xFE, 0x90, 0x38, 0xFA,  // wait for VBlank
    0xAF, 0xE0, 0x40,             // LCDC = 0

    0x21, 0x00, 0x02,             // LD HL,$0200 (tile source)
    0x11, 0x00, 0x80,             // LD DE,$8000 (tile VRAM)
    0x01, 0x00, 0x02,             // LD BC,$0200 (512 bytes)
    0x2A, 0x12, 0x13, 0x0B, 0x78, 0xB1, 0x20, 0xF8,

    0x21, 0x00, 0x04,             // LD HL,$0400 (map source)
    0x11, 0x00, 0x98,             // LD DE,$9800 (BG map)
    0x01, 0x00, 0x04,             // LD BC,$0400 (1024 bytes)
    0x2A, 0x12, 0x13, 0x0B, 0x78, 0xB1, 0x20, 0xF8,

    0x3E, 0x1B, 0xE0, 0x47,       // white background, black glyphs
    0x3E, 0x91, 0xE0, 0x40,       // LCDC = LCD on + BG on

    0xF0, 0x44, 0xFE, 0x90, 0x38, 0xFA,  // wait until VBlank starts
    0xF0, 0x44, 0xFE, 0x90, 0x30, 0xFA,  // wait until VBlank ends
    0x18, 0xF2,                    // repeat
};

uint8_t g_demo_rom[kDemoRomSize];
bool g_demo_rom_ready = false;

void build_font_tiles() {
  uint8_t *tiles = &g_demo_rom[kTileDataAddress];
  memset(tiles, 0, 32U * 16U);

  for (uint8_t letter = 0; letter < 26U; ++letter) {
    uint8_t *tile = tiles + static_cast<size_t>(letter + 1U) * 16U;
    for (uint8_t row = 0; row < 7U; ++row) {
      uint8_t pixels = 0;
      for (uint8_t column = 0; column < 5U; ++column) {
        if ((kFont[letter][column] & (1U << row)) != 0U) {
          pixels |= static_cast<uint8_t>(0x40U >> column);
        }
      }
      tile[static_cast<size_t>(row) * 2U] = pixels;
      tile[static_cast<size_t>(row) * 2U + 1U] = pixels;
    }
  }

  // Tile 27: horizontal line. Tile 28: vertical line. Tile 29: corner.
  tiles[(27U * 16U) + 6U] = 0xFF;
  tiles[(27U * 16U) + 7U] = 0xFF;
  for (uint8_t row = 0; row < 8U; ++row) {
    tiles[(28U * 16U) + (row * 2U)] = 0x18;
    tiles[(28U * 16U) + (row * 2U) + 1U] = 0x18;
  }
  for (uint8_t row = 0; row < 8U; ++row) {
    const uint8_t pixels = (row == 3U) ? 0xFFU : 0x18U;
    tiles[(29U * 16U) + (row * 2U)] = pixels;
    tiles[(29U * 16U) + (row * 2U) + 1U] = pixels;
  }

  // Tile 30: a small smiling player icon.
  constexpr uint8_t kPlayer[8] = {0x3C, 0x42, 0xA5, 0x81, 0xA5, 0x99, 0x42, 0x3C};
  for (uint8_t row = 0; row < 8U; ++row) {
    tiles[(30U * 16U) + (row * 2U)] = kPlayer[row];
    tiles[(30U * 16U) + (row * 2U) + 1U] = kPlayer[row];
  }
}

void write_map_text(uint8_t *map, uint8_t row, uint8_t column, const char *text) {
  while (*text != '\0' && column < 32U) {
    const char ch = *text++;
    map[static_cast<size_t>(row) * 32U + column] =
        (ch >= 'A' && ch <= 'Z') ? static_cast<uint8_t>(ch - 'A' + 1) : 0U;
    ++column;
  }
}

void build_background_map() {
  uint8_t *map = &g_demo_rom[kMapDataAddress];
  memset(map, 0, 32U * 32U);

  for (uint8_t column = 0U; column < 20U; ++column) {
    map[column] = 27U;
    map[(17U * 32U) + column] = 27U;
  }
  for (uint8_t row = 1U; row < 17U; ++row) {
    map[static_cast<size_t>(row) * 32U] = 28U;
    map[static_cast<size_t>(row) * 32U + 19U] = 28U;
  }
  map[0U] = 29U;
  map[19U] = 29U;
  map[(17U * 32U)] = 29U;
  map[(17U * 32U) + 19U] = 29U;

  write_map_text(map, 3U, 4U, "T5S3 GAMEBOY");
  write_map_text(map, 6U, 6U, "EPD DEMO");
  write_map_text(map, 11U, 4U, "TOUCH READY");
  write_map_text(map, 14U, 5U, "HOME BREW");
  map[(8U * 32U) + 9U] = 30U;
}

void build_demo_rom() {
  if (g_demo_rom_ready) {
    return;
  }

  memset(g_demo_rom, 0x00, sizeof(g_demo_rom));
  g_demo_rom[0x0100] = 0xC3;
  g_demo_rom[0x0101] = 0x50;
  g_demo_rom[0x0102] = 0x01;
  memcpy(&g_demo_rom[0x0104], kNintendoLogo, sizeof(kNintendoLogo));
  memcpy(&g_demo_rom[0x0134], "T5S3 GAMEBOY", 12);

  g_demo_rom[0x0143] = 0x00;
  g_demo_rom[0x0144] = 0x00;
  g_demo_rom[0x0145] = 0x00;
  g_demo_rom[0x0146] = 0x00;
  g_demo_rom[0x0147] = 0x00;
  g_demo_rom[0x0148] = 0x00;
  g_demo_rom[0x0149] = 0x00;
  g_demo_rom[0x014A] = 0x01;
  g_demo_rom[0x014B] = 0x00;
  g_demo_rom[0x014C] = 0x00;

  memcpy(&g_demo_rom[0x0150], kProgram, sizeof(kProgram));
  build_font_tiles();
  build_background_map();

  uint8_t header_checksum = 0;
  for (uint16_t i = kHeaderChecksumStart; i <= kHeaderChecksumEnd; ++i) {
    header_checksum = static_cast<uint8_t>(header_checksum - g_demo_rom[i] - 1U);
  }
  g_demo_rom[0x014D] = header_checksum;

  uint16_t global_checksum = 0;
  for (uint16_t i = 0; i < kDemoRomSize; ++i) {
    if (i != kGlobalChecksumHigh && i != kGlobalChecksumLow) {
      global_checksum = static_cast<uint16_t>(global_checksum + g_demo_rom[i]);
    }
  }
  g_demo_rom[kGlobalChecksumHigh] = static_cast<uint8_t>(global_checksum >> 8);
  g_demo_rom[kGlobalChecksumLow] = static_cast<uint8_t>(global_checksum & 0xFFU);
  g_demo_rom_ready = true;
}

}  // namespace

const uint8_t *builtin_demo_rom_data() {
  build_demo_rom();
  return g_demo_rom;
}

size_t builtin_demo_rom_size() {
  return kDemoRomSize;
}

const char *builtin_demo_rom_name() {
  return "Bundled T5S3-GameBoy homebrew demo";
}
