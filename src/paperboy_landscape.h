#pragma once
#include "paperboy_ui.h"
#include "paperboy_orientation.h"
static constexpr uint16_t PAPERBOY_LANDSCAPE_GAME_Y = 54;
void paperboy_landscape_draw(uint8_t *canvas, uint8_t *panel, const uint8_t *game,
                            uint8_t buttons, bool power_on, bool save_available,
                            const PaperboyBatteryStatus *battery, const char *notice);
void paperboy_landscape_game(const uint8_t *game, uint8_t *panel);
uint8_t paperboy_landscape_buttons(const touch_state_t *touch);
uint32_t paperboy_landscape_actions(const touch_state_t *touch);
