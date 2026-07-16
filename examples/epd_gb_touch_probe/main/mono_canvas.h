#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void mono_clear(uint8_t *buffer, size_t size, bool white);
void mono_put_pixel(
    uint8_t *buffer,
    uint16_t pitch_bytes,
    uint16_t width,
    uint16_t height,
    int x,
    int y,
    bool white);
void mono_fill_rect(
    uint8_t *buffer,
    uint16_t pitch_bytes,
    uint16_t width,
    uint16_t height,
    int x,
    int y,
    int rect_width,
    int rect_height,
    bool white);
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
    bool white);
void mono_draw_line(
    uint8_t *buffer,
    uint16_t pitch_bytes,
    uint16_t width,
    uint16_t height,
    int x0,
    int y0,
    int x1,
    int y1,
    bool white);
void mono_draw_text(
    uint8_t *buffer,
    uint16_t pitch_bytes,
    uint16_t width,
    uint16_t height,
    int x,
    int y,
    const char *text,
    uint8_t scale,
    bool white);

#ifdef __cplusplus
}
#endif
