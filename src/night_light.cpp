#include "night_light.h"

#include <Arduino.h>
#include <Preferences.h>
#include <driver/ledc.h>
#include <esp_err.h>
#include <esp_log.h>

#include "t5s3_epd_pins.h"

namespace {

constexpr char kTag[] = "night_light";
constexpr char kPreferencesNamespace[] = "gb-light";
constexpr char kBrightnessKey[] = "level";
constexpr ledc_mode_t kMode = LEDC_LOW_SPEED_MODE;
// Game audio owns LEDC timer 0/channel 0. Never share its timer: the
// audio engine changes that timer's frequency dynamically.
constexpr ledc_timer_t kTimer = LEDC_TIMER_2;
constexpr ledc_channel_t kChannel = LEDC_CHANNEL_2;
constexpr uint32_t kPwmHz = 1000U;  // PT4103B23F EN: <= approximately 1 kHz.
constexpr uint32_t kDutyMax = 1023U;
constexpr uint8_t kMaxBrightnessPercent = 10U;

bool g_ready = false;
uint8_t g_brightness = 0U;

bool apply_brightness(uint8_t brightness) {
  // 0..10 represents actual PWM percentage, not a remapped 0..100 scale.
  const uint32_t duty =
      (static_cast<uint32_t>(brightness) * kDutyMax + 50U) / 100U;
  const esp_err_t set_result = ledc_set_duty(kMode, kChannel, duty);
  if (set_result != ESP_OK) {
    ESP_LOGE(kTag, "set duty failed: %s", esp_err_to_name(set_result));
    return false;
  }
  const esp_err_t update_result = ledc_update_duty(kMode, kChannel);
  if (update_result != ESP_OK) {
    ESP_LOGE(kTag, "update duty failed: %s", esp_err_to_name(update_result));
    return false;
  }
  return true;
}

}  // namespace

void night_light_init() {
  if (g_ready) {
    return;
  }

  // Keep the LED boost converter off until both the pin and timer are ready.
  pinMode(t5s3_epd::kBacklightEnable, OUTPUT);
  digitalWrite(t5s3_epd::kBacklightEnable, LOW);

  uint8_t saved = 0U;
  Preferences preferences;
  if (preferences.begin(kPreferencesNamespace, true)) {
    saved = preferences.getUChar(kBrightnessKey, 0U);
    preferences.end();
  } else {
    ESP_LOGW(kTag, "NVS unavailable; starting with light off");
  }
  // Old firmware allowed 0..100: cap a previously saved brighter setting
  // rather than switching off or briefly driving the LEDs too brightly.
  if (saved > kMaxBrightnessPercent) {
    saved = kMaxBrightnessPercent;
  }

  ledc_timer_config_t timer = {};
  timer.speed_mode = kMode;
  timer.timer_num = kTimer;
  timer.duty_resolution = LEDC_TIMER_10_BIT;
  timer.freq_hz = kPwmHz;
  timer.clk_cfg = LEDC_AUTO_CLK;
  esp_err_t result = ledc_timer_config(&timer);
  if (result != ESP_OK) {
    ESP_LOGE(kTag, "PWM timer init failed: %s", esp_err_to_name(result));
    return;
  }

  ledc_channel_config_t channel = {};
  channel.gpio_num = t5s3_epd::kBacklightEnable;
  channel.speed_mode = kMode;
  channel.channel = kChannel;
  channel.intr_type = LEDC_INTR_DISABLE;
  channel.timer_sel = kTimer;
  channel.duty = 0U;
  channel.hpoint = 0U;
  result = ledc_channel_config(&channel);
  if (result != ESP_OK) {
    ESP_LOGE(kTag, "PWM channel init failed: %s", esp_err_to_name(result));
    digitalWrite(t5s3_epd::kBacklightEnable, LOW);
    return;
  }

  g_ready = true;
  if (apply_brightness(saved)) {
    g_brightness = saved;
  }
  ESP_LOGI(kTag, "ready gpio=%u PWM=%luHz brightness=%u%% (max 10%%)",
           t5s3_epd::kBacklightEnable,
           static_cast<unsigned long>(kPwmHz),
           static_cast<unsigned>(g_brightness));
}

uint8_t night_light_brightness() {
  return g_brightness;
}

bool night_light_set_brightness(uint8_t percent) {
  if (!g_ready) {
    return false;
  }
  if (percent > kMaxBrightnessPercent) {
    percent = kMaxBrightnessPercent;
  }
  if (percent == g_brightness) {
    return true;
  }
  if (!apply_brightness(percent)) {
    return false;
  }
  g_brightness = percent;
  Preferences preferences;
  if (preferences.begin(kPreferencesNamespace, false)) {
    if (preferences.putUChar(kBrightnessKey, percent) != sizeof(uint8_t)) {
      ESP_LOGW(kTag, "could not persist brightness; change remains active");
    }
    preferences.end();
  } else {
    ESP_LOGW(kTag, "NVS unavailable; brightness change remains active");
  }
  ESP_LOGI(kTag, "brightness=%u%%", static_cast<unsigned>(percent));
  return true;
}

void night_light_shutdown() {
  if (g_ready) {
    (void)ledc_stop(kMode, kChannel, 0U);
  }
  g_ready = false;
  g_brightness = 0U;
  pinMode(t5s3_epd::kBacklightEnable, OUTPUT);
  digitalWrite(t5s3_epd::kBacklightEnable, LOW);
}
