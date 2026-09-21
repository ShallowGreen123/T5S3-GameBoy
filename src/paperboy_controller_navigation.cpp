#include "paperboy_ui.h"

namespace {
uint8_t selection = 0U;
uint8_t previous = 0U;
uint8_t direction = 0U;
uint32_t repeat_at = 0U;
bool wait_release = false;
}

void paperboy_ui_controller_page_changed() {
  wait_release = true;
  direction = 0U;
}

bool paperboy_ui_controller_ready() { return !wait_release; }
uint8_t paperboy_ui_controller_selection() { return selection; }

uint32_t paperboy_ui_map_controller(uint8_t buttons, PaperboyPage page, uint32_t now) {
  const uint8_t rising = buttons & ~previous;
  previous = buttons;
  if (wait_release) {
    if (buttons == 0U) wait_release = false;
    return 0U;
  }
  if (page == PaperboyPage::Game) {
    direction = 0U;
    return 0U;
  }
  if (rising & GBEMU_INPUT_B) return PAPERBOY_ACTION_BACK;
  uint8_t held = buttons & (GBEMU_INPUT_UP | GBEMU_INPUT_DOWN);
  if (held == (GBEMU_INPUT_UP | GBEMU_INPUT_DOWN)) held = 0U;
  bool move = false;
  if (held != direction) {
    direction = held;
    repeat_at = now + 500U;
    move = held != 0U;
  } else if (held && static_cast<int32_t>(now - repeat_at) >= 0) {
    repeat_at = now + 150U;
    move = true;
  }
  if (page == PaperboyPage::Settings) {
    if (move) selection = (selection + (held == GBEMU_INPUT_UP ? 2U : 1U)) % 3U;
    if (rising & GBEMU_INPUT_A) {
      const uint32_t options[] = {PAPERBOY_ACTION_BATTERY, PAPERBOY_ACTION_SD_CARD,
                                  PAPERBOY_ACTION_ABOUT};
      return options[selection];
    }
    return move ? static_cast<uint32_t>(PAPERBOY_ACTION_REFRESH) : 0U;
  }
  if (page == PaperboyPage::SdCard) {
    if (rising & GBEMU_INPUT_A) return PAPERBOY_ACTION_ROM_LAUNCH;
    if (move) return held == GBEMU_INPUT_UP ? PAPERBOY_ACTION_ROM_PREVIOUS
                                          : PAPERBOY_ACTION_ROM_NEXT;
  }
  return 0U;
}
