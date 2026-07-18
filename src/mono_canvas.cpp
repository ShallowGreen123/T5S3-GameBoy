#include "mono_canvas.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

namespace {

const uint8_t *glyph_for(char ch) {
  static const uint8_t kBlank[5] = {0x00, 0x00, 0x00, 0x00, 0x00};
  static const uint8_t kDash[5] = {0x08, 0x08, 0x08, 0x08, 0x08};
  static const uint8_t k0[5] = {0x3E, 0x51, 0x49, 0x45, 0x3E};
  static const uint8_t k1[5] = {0x00, 0x42, 0x7F, 0x40, 0x00};
  static const uint8_t k2[5] = {0x62, 0x51, 0x49, 0x49, 0x46};
  static const uint8_t k3[5] = {0x22, 0x49, 0x49, 0x49, 0x36};
  static const uint8_t k4[5] = {0x18, 0x14, 0x12, 0x7F, 0x10};
  static const uint8_t k5[5] = {0x2F, 0x49, 0x49, 0x49, 0x31};
  static const uint8_t k6[5] = {0x3E, 0x49, 0x49, 0x49, 0x32};
  static const uint8_t k7[5] = {0x01, 0x71, 0x09, 0x05, 0x03};
  static const uint8_t k8[5] = {0x36, 0x49, 0x49, 0x49, 0x36};
  static const uint8_t k9[5] = {0x26, 0x49, 0x49, 0x49, 0x3E};
  static const uint8_t kA[5] = {0x7E, 0x09, 0x09, 0x09, 0x7E};
  static const uint8_t kB[5] = {0x7F, 0x49, 0x49, 0x49, 0x36};
  static const uint8_t kC[5] = {0x3E, 0x41, 0x41, 0x41, 0x22};
  static const uint8_t kD[5] = {0x7F, 0x41, 0x41, 0x22, 0x1C};
  static const uint8_t kE[5] = {0x7F, 0x49, 0x49, 0x49, 0x41};
  static const uint8_t kF[5] = {0x7F, 0x09, 0x09, 0x09, 0x01};
  static const uint8_t kG[5] = {0x3E, 0x41, 0x49, 0x49, 0x7A};
  static const uint8_t kH[5] = {0x7F, 0x08, 0x08, 0x08, 0x7F};
  static const uint8_t kI[5] = {0x00, 0x41, 0x7F, 0x41, 0x00};
  static const uint8_t kJ[5] = {0x20, 0x40, 0x41, 0x3F, 0x01};
  static const uint8_t kK[5] = {0x7F, 0x08, 0x14, 0x22, 0x41};
  static const uint8_t kL[5] = {0x7F, 0x40, 0x40, 0x40, 0x40};
  static const uint8_t kM[5] = {0x7F, 0x02, 0x0C, 0x02, 0x7F};
  static const uint8_t kN[5] = {0x7F, 0x04, 0x08, 0x10, 0x7F};
  static const uint8_t kO[5] = {0x3E, 0x41, 0x41, 0x41, 0x3E};
  static const uint8_t kP[5] = {0x7F, 0x09, 0x09, 0x09, 0x06};
  static const uint8_t kQ[5] = {0x3E, 0x41, 0x51, 0x21, 0x5E};
  static const uint8_t kR[5] = {0x7F, 0x09, 0x19, 0x29, 0x46};
  static const uint8_t kS[5] = {0x26, 0x49, 0x49, 0x49, 0x32};
  static const uint8_t kT[5] = {0x01, 0x01, 0x7F, 0x01, 0x01};
  static const uint8_t kU[5] = {0x3F, 0x40, 0x40, 0x40, 0x3F};
  static const uint8_t kV[5] = {0x1F, 0x20, 0x40, 0x20, 0x1F};
  static const uint8_t kW[5] = {0x7F, 0x20, 0x18, 0x20, 0x7F};
  static const uint8_t kX[5] = {0x63, 0x14, 0x08, 0x14, 0x63};
  static const uint8_t kY[5] = {0x03, 0x04, 0x78, 0x04, 0x03};
  static const uint8_t kZ[5] = {0x61, 0x51, 0x49, 0x45, 0x43};

  switch (toupper(static_cast<unsigned char>(ch))) {
    case '-':
      return kDash;
    case '0':
      return k0;
    case '1':
      return k1;
    case '2':
      return k2;
    case '3':
      return k3;
    case '4':
      return k4;
    case '5':
      return k5;
    case '6':
      return k6;
    case '7':
      return k7;
    case '8':
      return k8;
    case '9':
      return k9;
    case 'A':
      return kA;
    case 'B':
      return kB;
    case 'C':
      return kC;
    case 'D':
      return kD;
    case 'E':
      return kE;
    case 'F':
      return kF;
    case 'G':
      return kG;
    case 'H':
      return kH;
    case 'I':
      return kI;
    case 'J':
      return kJ;
    case 'K':
      return kK;
    case 'L':
      return kL;
    case 'M':
      return kM;
    case 'N':
      return kN;
    case 'O':
      return kO;
    case 'P':
      return kP;
    case 'Q':
      return kQ;
    case 'R':
      return kR;
    case 'S':
      return kS;
    case 'T':
      return kT;
    case 'U':
      return kU;
    case 'V':
      return kV;
    case 'W':
      return kW;
    case 'X':
      return kX;
    case 'Y':
      return kY;
    case 'Z':
      return kZ;
    default:
      return kBlank;
  }
}

}  // namespace

void mono_clear(uint8_t *buffer, size_t size, bool white) {
  if (buffer == nullptr || size == 0U) {
    return;
  }
  memset(buffer, white ? 0xFF : 0x00, size);
}

void mono_put_pixel(
    uint8_t *buffer,
    uint16_t pitch_bytes,
    uint16_t width,
    uint16_t height,
    int x,
    int y,
    bool white) {
  if (buffer == nullptr || x < 0 || y < 0 || x >= width || y >= height) {
    return;
  }

  const size_t offset = ((size_t)y * pitch_bytes) + (size_t)(x >> 3);
  const uint8_t mask = (uint8_t)(0x80U >> (x & 7));
  if (white) {
    buffer[offset] |= mask;
  } else {
    buffer[offset] &= (uint8_t)(~mask);
  }
}

void mono_fill_rect(
    uint8_t *buffer,
    uint16_t pitch_bytes,
    uint16_t width,
    uint16_t height,
    int x,
    int y,
    int rect_width,
    int rect_height,
    bool white) {
  for (int py = y; py < (y + rect_height); ++py) {
    for (int px = x; px < (x + rect_width); ++px) {
      mono_put_pixel(buffer, pitch_bytes, width, height, px, py, white);
    }
  }
}

void mono_draw_frame(
    uint8_t *buffer,
    uint16_t pitch_bytes,
    uint16_t width,
    uint16_t height,
    int x,
    int y,
    int rect_width,
    int rect_height,
    int thickness,
    bool white) {
  mono_fill_rect(buffer, pitch_bytes, width, height, x, y, rect_width, thickness, white);
  mono_fill_rect(
      buffer,
      pitch_bytes,
      width,
      height,
      x,
      y + rect_height - thickness,
      rect_width,
      thickness,
      white);
  mono_fill_rect(buffer, pitch_bytes, width, height, x, y, thickness, rect_height, white);
  mono_fill_rect(
      buffer,
      pitch_bytes,
      width,
      height,
      x + rect_width - thickness,
      y,
      thickness,
      rect_height,
      white);
}

void mono_draw_line(
    uint8_t *buffer,
    uint16_t pitch_bytes,
    uint16_t width,
    uint16_t height,
    int x0,
    int y0,
    int x1,
    int y1,
    bool white) {
  int dx = abs(x1 - x0);
  int sx = (x0 < x1) ? 1 : -1;
  int dy = -abs(y1 - y0);
  int sy = (y0 < y1) ? 1 : -1;
  int err = dx + dy;

  while (true) {
    mono_put_pixel(buffer, pitch_bytes, width, height, x0, y0, white);
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

void mono_draw_circle(
    uint8_t *buffer,
    uint16_t pitch_bytes,
    uint16_t width,
    uint16_t height,
    int center_x,
    int center_y,
    int radius,
    bool white) {
  if (buffer == nullptr || radius < 0) {
    return;
  }

  int x = radius;
  int y = 0;
  int error = 1 - radius;
  while (x >= y) {
    mono_put_pixel(buffer, pitch_bytes, width, height, center_x + x, center_y + y, white);
    mono_put_pixel(buffer, pitch_bytes, width, height, center_x + y, center_y + x, white);
    mono_put_pixel(buffer, pitch_bytes, width, height, center_x - y, center_y + x, white);
    mono_put_pixel(buffer, pitch_bytes, width, height, center_x - x, center_y + y, white);
    mono_put_pixel(buffer, pitch_bytes, width, height, center_x - x, center_y - y, white);
    mono_put_pixel(buffer, pitch_bytes, width, height, center_x - y, center_y - x, white);
    mono_put_pixel(buffer, pitch_bytes, width, height, center_x + y, center_y - x, white);
    mono_put_pixel(buffer, pitch_bytes, width, height, center_x + x, center_y - y, white);
    ++y;
    if (error < 0) {
      error += (2 * y) + 1;
    } else {
      --x;
      error += (2 * (y - x)) + 1;
    }
  }
}

void mono_fill_circle(
    uint8_t *buffer,
    uint16_t pitch_bytes,
    uint16_t width,
    uint16_t height,
    int center_x,
    int center_y,
    int radius,
    bool white) {
  if (buffer == nullptr || radius < 0) {
    return;
  }

  const int radius_squared = radius * radius;
  for (int y = -radius; y <= radius; ++y) {
    int half_width = radius;
    while (half_width > 0 && ((half_width * half_width) + (y * y)) > radius_squared) {
      --half_width;
    }
    mono_fill_rect(
        buffer,
        pitch_bytes,
        width,
        height,
        center_x - half_width,
        center_y + y,
        (half_width * 2) + 1,
        1,
        white);
  }
}

void mono_draw_text(
    uint8_t *buffer,
    uint16_t pitch_bytes,
    uint16_t width,
    uint16_t height,
    int x,
    int y,
    const char *text,
    uint8_t scale,
    bool white) {
  if (buffer == nullptr || text == nullptr || scale == 0U) {
    return;
  }

  int cursor_x = x;
  for (const char *ch = text; *ch != '\0'; ++ch) {
    const uint8_t *glyph = glyph_for(*ch);
    for (uint8_t column = 0; column < 5U; ++column) {
      for (uint8_t row = 0; row < 7U; ++row) {
        if ((glyph[column] & (1U << row)) == 0U) {
          continue;
        }
        mono_fill_rect(
            buffer,
            pitch_bytes,
            width,
            height,
            cursor_x + (int)column * scale,
            y + (int)row * scale,
            scale,
            scale,
            white);
      }
    }
    cursor_x += (int)(6U * scale);
  }
}
