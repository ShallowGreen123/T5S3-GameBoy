#include "paperboy_landscape.h"
#include "mono_canvas.h"
#include <stdio.h>
#include <string.h>

namespace {
constexpr int width = 960, height = 540, pitch = 120;
constexpr int game_x = 240, game_y = PAPERBOY_LANDSCAPE_GAME_Y;
struct Rect { int x, y, w, h; };
constexpr Rect power{16, 10, 192, 36}, save{240, 10, 100, 36}, load{620, 10, 100, 36};
constexpr Rect rotate{752, 10, 192, 36}, settings{404, 494, 152, 36};
constexpr Rect selectBtn{40, 432, 144, 46}, start{776, 432, 144, 46};
constexpr Rect light_down{24, 72, 80, 40}, light_up{124, 72, 80, 40};
bool inside(int x, int y, Rect r) { return x >= r.x && y >= r.y && x < r.x+r.w && y < r.y+r.h; }
bool circle(int x, int y, int cx, int cy, int radius) { return (x-cx)*(x-cx)+(y-cy)*(y-cy) <= radius*radius; }
void text(uint8_t *p, int x, int y, const char *s, int scale=2) { mono_draw_text(p,pitch,width,height,x,y,s,scale,false); }
void box(uint8_t *p, Rect r, const char *s, bool pressed=false) {
  mono_draw_frame(p,pitch,width,height,r.x,r.y,r.w,r.h,pressed?5:2,false);
  text(p,r.x+10,r.y+12,s);
}
uint8_t reverse_bits(uint8_t b) {
  b = (b >> 4) | (b << 4);
  b = ((b & 0xCC) >> 2) | ((b & 0x33) << 2);
  return ((b & 0xAA) >> 1) | ((b & 0x55) << 1);
}
void panel_byte(uint8_t *panel, int offset, uint8_t value) {
  if (paperboy_orientation() == PaperboyOrientation::LandscapeReverse)
    panel[width*height/8-1-offset] = static_cast<uint8_t>(~reverse_bits(value));
  else panel[offset] = static_cast<uint8_t>(~value);
}
}
uint8_t paperboy_landscape_buttons(const touch_state_t *touch) {
  if (!touch || !touch->touched) return 0;
  uint8_t buttons = 0;
  for (uint8_t i=0; i<touch->points; ++i) {
    uint16_t x,y; paperboy_landscape_touch(touch->x[i],touch->y[i],x,y);
    const int dx=int(x)-116, dy=int(y)-270;
    if (circle(x,y,116,270,104)) {
      if (dx < -22) buttons |= GBEMU_INPUT_LEFT;
      if (dx > 22) buttons |= GBEMU_INPUT_RIGHT;
      if (dy < -22) buttons |= GBEMU_INPUT_UP;
      if (dy > 22) buttons |= GBEMU_INPUT_DOWN;
    }
    if (circle(x,y,864,230,48)) buttons |= GBEMU_INPUT_A;
    if (circle(x,y,788,326,48)) buttons |= GBEMU_INPUT_B;
    if (inside(x,y,selectBtn)) buttons |= GBEMU_INPUT_SELECT;
    if (inside(x,y,start)) buttons |= GBEMU_INPUT_START;
  }
  return buttons;
}
uint32_t paperboy_landscape_actions(const touch_state_t *touch) {
  if (!touch || !touch->touched) return 0;
  uint32_t actions = 0;
  for (uint8_t i=0; i<touch->points; ++i) {
    uint16_t x,y; paperboy_landscape_touch(touch->x[i],touch->y[i],x,y);
    if (inside(x,y,power)) actions |= PAPERBOY_ACTION_POWER;
    if (inside(x,y,save)) actions |= PAPERBOY_ACTION_SAVE;
    if (inside(x,y,load)) actions |= PAPERBOY_ACTION_LOAD;
    if (inside(x,y,rotate)) actions |= PAPERBOY_ACTION_ROTATE;
    if (inside(x,y,settings)) actions |= PAPERBOY_ACTION_SETTINGS;
    if (inside(x,y,light_down)) actions |= 1UL<<17;
    if (inside(x,y,light_up)) actions |= 1UL<<18;
  }
  return actions;
}
void paperboy_landscape_game(const uint8_t *game, uint8_t *panel) {
  for (unsigned y=0; y<GBEMU_FRAME_HEIGHT; ++y)
    for (unsigned x=0; x<GBEMU_FRAME_PITCH_BYTES; ++x)
      panel_byte(panel,(y+game_y)*pitch+game_x/8+x,game[y*GBEMU_FRAME_PITCH_BYTES+x]);
}
void paperboy_landscape_draw(uint8_t *canvas, uint8_t *panel, const uint8_t *game,
                            uint8_t buttons, bool power_on, bool save_available,
                            const PaperboyBatteryStatus *battery, const char *notice) {
  mono_clear(canvas,width*height/8,true);
  mono_draw_frame(canvas,pitch,width,height,game_x-4,game_y-4,488,440,3,false);
  box(canvas,power,power_on?"ON/OFF":"POWER OFF"); box(canvas,save,"SAVE");
  box(canvas,load,save_available?"LOAD":"NO SAVE"); box(canvas,rotate,"ROTATE");
  box(canvas,settings,"SETTINGS");
  box(canvas,selectBtn,"SELECT",buttons & GBEMU_INPUT_SELECT);
  box(canvas,start,"START",buttons & GBEMU_INPUT_START);
  box(canvas,light_down,"-"); box(canvas,light_up,"+");
  text(canvas,52,126,"LIGHT",1);
  mono_fill_rect(canvas,pitch,width,height,86,174,60,192,false);
  mono_fill_rect(canvas,pitch,width,height,20,240,192,60,false);
  const uint8_t bits[] = {GBEMU_INPUT_UP,GBEMU_INPUT_DOWN,GBEMU_INPUT_LEFT,GBEMU_INPUT_RIGHT};
  const int xs[] = {116,116,38,194}, ys[] = {192,348,270,270};
  for (int i=0;i<4;++i) mono_fill_circle(canvas,pitch,width,height,xs[i],ys[i],10,!(buttons & bits[i]));
  mono_draw_circle(canvas,pitch,width,height,864,230,38,false);
  mono_draw_circle(canvas,pitch,width,height,788,326,38,false);
  if (buttons & GBEMU_INPUT_A) mono_draw_circle(canvas,pitch,width,height,864,230,30,false);
  if (buttons & GBEMU_INPUT_B) mono_draw_circle(canvas,pitch,width,height,788,326,30,false);
  text(canvas,853,223,"A",3); text(canvas,777,319,"B",3);
  if (battery) { char label[24]; snprintf(label,sizeof(label),"BAT %u%%",battery->soc_percent); text(canvas,414,23,label); }

  if (!power_on) text(canvas,390,255,"POWER OFF",3);
  if (notice) text(canvas,24,508,notice,1);
  for (int i=0;i<width*height/8;++i) panel_byte(panel,i,canvas[i]);
  if (power_on) paperboy_landscape_game(game,panel);
}
