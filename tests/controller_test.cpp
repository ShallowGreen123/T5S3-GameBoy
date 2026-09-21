#include <cassert>
#include <Arduino.h>
#include <Wire.h>
#include "gbemu.h"
#include "paperboy_ui.h"
#include "snes_mini_controller.h"
uint32_t test_now = 1000;
TestWire Wire;
uint8_t poll(uint16_t pressed, uint32_t dt = 16) {
  test_now += dt;
  Wire.pressed = pressed;
  return snes_mini_controller_buttons();
}
int main() {
  // L+R+Right rotates once, consumes gameplay input, and permits Right re-taps.
  assert(poll(0xA200) == 0);
  assert(snes_mini_controller_take_actions() == SNES_ACTION_ROTATE);
  assert(snes_mini_controller_navigation_buttons() == 0);
  assert(poll(0xA200, 600) == 0); assert(snes_mini_controller_take_actions() == 0);
  poll(0x2200); assert(snes_mini_controller_take_actions() == 0);
  poll(0xA200); assert(snes_mini_controller_take_actions() == SNES_ACTION_ROTATE);
  assert(poll(0x8200) == 0); assert(snes_mini_controller_take_actions() == 0);
  assert(poll(0x8000) == 0); assert(snes_mini_controller_take_actions() == 0);
  poll(0);
  assert(poll(0x8000) == GBEMU_INPUT_RIGHT);
  poll(0);
  // Settings wins if both complete shortcuts are pressed together.
  poll(0xB600); assert(snes_mini_controller_take_actions() == SNES_ACTION_SETTINGS);
  poll(0);

  // Settings chord wins over save/load/brightness, including staggered release.
  poll(0x3600); assert(snes_mini_controller_take_actions() == SNES_ACTION_SETTINGS);
  assert(snes_mini_controller_navigation_buttons() == 0);
  poll(0x3600); assert(snes_mini_controller_take_actions() == 0);
  assert(poll(0x1600) == 0); assert(snes_mini_controller_take_actions() == 0);
  assert(poll(0x0200) == 0); assert(snes_mini_controller_take_actions() == 0);
  poll(0);
  poll(0x1400); poll(0x3400); assert(snes_mini_controller_take_actions() == 0);
  poll(0x3600); assert(snes_mini_controller_take_actions() == SNES_ACTION_SETTINGS);
  poll(0);
  // Turbo must never activate menu entries.
  poll(0x0008 | 0x0020); assert(snes_mini_controller_navigation_buttons() == 0);
  poll(0);

  assert(poll(0x0010 | 0x0040 | 0x0400 | 0x1000) ==
         (GBEMU_INPUT_A | GBEMU_INPUT_B | GBEMU_INPUT_START | GBEMU_INPUT_SELECT));
  poll(0);
  poll(0x0200); assert(snes_mini_controller_take_actions() == SNES_ACTION_SAVE);
  assert(snes_mini_controller_take_actions() == 0);
  poll(0x0200); assert(snes_mini_controller_take_actions() == 0);
  poll(0); poll(0x2000); assert(snes_mini_controller_take_actions() == SNES_ACTION_LOAD);
  poll(0);
  assert(poll(0x1000 | 0x0200) == 0);
  assert(snes_mini_controller_take_actions() == SNES_ACTION_BRIGHTEN);
  poll(0x0200); assert(snes_mini_controller_take_actions() == 0);
  poll(0);
  assert(poll(0x1000 | 0x2000) == 0);
  assert(snes_mini_controller_take_actions() == SNES_ACTION_DIM);
  assert(poll(0x1000) == 0);
  poll(0); assert(poll(0x1000) == GBEMU_INPUT_SELECT);
  poll(0); poll(0x2200); assert(snes_mini_controller_take_actions() == 0);
  poll(0x2000); assert(snes_mini_controller_take_actions() == 0);
  poll(0);
  assert(poll(0x0008 | 0x0020) == (GBEMU_INPUT_A | GBEMU_INPUT_B));
  assert(poll(0x0008 | 0x0020, 50) == 0);
  assert(poll(0x0008 | 0x0020, 50) == (GBEMU_INPUT_A | GBEMU_INPUT_B));
  assert(poll(0x0008 | 0x0020 | 0x0010 | 0x0040, 50) == (GBEMU_INPUT_A | GBEMU_INPUT_B));
  poll(0);
  test_now = UINT32_MAX - 40;
  assert(poll(0x0008) == GBEMU_INPUT_A);
  assert(poll(0x0008, 50) == 0);
  assert(poll(0x0008, 50) == GBEMU_INPUT_A);
  Wire.connected = false;
  assert(poll(0x0008) == 0);
  assert(snes_mini_controller_take_actions() == 0);
  Wire.connected = true;
  assert(poll(0, 1000) == 0);
  poll(0x0200); // A cached poll must not replay an unconsumed action.
  poll(0x0200, 1); assert(snes_mini_controller_take_actions() == 0);
  auto nav = paperboy_ui_map_controller;
  assert(nav(GBEMU_INPUT_DOWN, PaperboyPage::Settings, 1000) == PAPERBOY_ACTION_REFRESH);
  assert(paperboy_ui_controller_selection() == 1);
  assert(nav(GBEMU_INPUT_DOWN, PaperboyPage::Settings, 1499) == 0);
  assert(nav(GBEMU_INPUT_DOWN, PaperboyPage::Settings, 1500) == PAPERBOY_ACTION_REFRESH);
  assert(paperboy_ui_controller_selection() == 2);
  nav(0, PaperboyPage::Settings, 1510);
  nav(GBEMU_INPUT_DOWN, PaperboyPage::Settings, 1520);
  assert(paperboy_ui_controller_selection() == 0);
  nav(0, PaperboyPage::Settings, 1530);
  nav(GBEMU_INPUT_UP, PaperboyPage::Settings, 1540);
  assert(paperboy_ui_controller_selection() == 2);
  assert(nav(GBEMU_INPUT_A, PaperboyPage::Settings, 1550) == PAPERBOY_ACTION_ABOUT);
  assert(nav(GBEMU_INPUT_A, PaperboyPage::Settings, 1560) == 0);
  paperboy_ui_controller_page_changed();
  assert(nav(GBEMU_INPUT_A, PaperboyPage::About, 1570) == 0);
  assert(!paperboy_ui_controller_ready());
  nav(0, PaperboyPage::About, 1580);
  assert(paperboy_ui_controller_ready());
  assert(nav(GBEMU_INPUT_B, PaperboyPage::About, 1590) == PAPERBOY_ACTION_BACK);
  paperboy_ui_controller_page_changed();
  assert(nav(GBEMU_INPUT_B, PaperboyPage::Settings, 1600) == 0);
  nav(0, PaperboyPage::Settings, 1610);
  assert(nav(GBEMU_INPUT_B, PaperboyPage::Settings, 1620) == PAPERBOY_ACTION_BACK);
  paperboy_ui_controller_page_changed();
  nav(GBEMU_INPUT_B, PaperboyPage::Game, 1630);
  assert(!paperboy_ui_controller_ready());
  nav(0, PaperboyPage::Game, 1640);
  assert(paperboy_ui_controller_ready());
  assert(nav(GBEMU_INPUT_DOWN, PaperboyPage::SdCard, 1700) == PAPERBOY_ACTION_ROM_NEXT);
  assert(nav(GBEMU_INPUT_UP, PaperboyPage::SdCard, 1710) == PAPERBOY_ACTION_ROM_PREVIOUS);
  assert(nav(GBEMU_INPUT_A, PaperboyPage::SdCard, 1720) == PAPERBOY_ACTION_ROM_LAUNCH);
  assert(nav(GBEMU_INPUT_A, PaperboyPage::SdCard, 1730) == 0);
  nav(0, PaperboyPage::Settings, 1740);
  nav(GBEMU_INPUT_UP, PaperboyPage::Settings, 1750);
  assert(nav(GBEMU_INPUT_A, PaperboyPage::Settings, 1760) == PAPERBOY_ACTION_SD_CARD);
  nav(GBEMU_INPUT_UP, PaperboyPage::Settings, 1770);
  assert(nav(GBEMU_INPUT_A, PaperboyPage::Settings, 1780) == PAPERBOY_ACTION_BATTERY);
}
