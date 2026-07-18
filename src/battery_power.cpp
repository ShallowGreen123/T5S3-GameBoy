#include "battery_power.h"

#include <Arduino.h>
#include <Wire.h>
#include <bq25896.h>
#include <bq25896_hal_esp_idf.h>
#include <bq27220.h>
#include <esp_log.h>

namespace {

constexpr char kTag[] = "battery";
constexpr uint8_t kBq27220Address = 0x55;
constexpr uint8_t kBq25896Address = 0x6B;
constexpr uint32_t kI2cFrequencyHz = 400000U;
constexpr uint32_t kChargerServicePeriodMs = 30000U;

struct BatteryProfile {
  uint16_t input_limit_ma;
  uint16_t capacity_mah;
  uint16_t charge_current_ma;
  uint16_t precharge_current_ma;
  uint16_t termination_current_ma;
  uint16_t charge_voltage_mv;
  uint16_t charge_termination_voltage_delta_mv;
  uint16_t system_min_voltage_mv;
  int16_t current_threshold_ma;
};

constexpr BatteryProfile kProfile = {
    1000,
    1500,
    512,
    64,
    64,
    4208,
    100,
    3300,
    20,
};

bool g_battery_init_attempted = false;
bool g_charger_found = false;
bool g_gauge_found = false;
bool g_charger_ready = false;
bool g_gauge_ready = false;
uint32_t g_last_charger_service_ms = 0;
bq25896_hal_esp_idf_ctx_t g_charger_hal = {};
bq25896_t g_charger = {};
BQ27220 g_gauge;

bool probe(uint8_t address) {
  Wire.beginTransmission(address);
  return Wire.endTransmission() == 0;
}

i2c_master_bus_handle_t i2c_bus_handle() {
  return reinterpret_cast<i2c_master_bus_handle_t>(&Wire);
}

bool charger_call_ok(bq25896_err_t result) {
  return BQ25896_SUCCEEDED(result);
}

bool configure_charger() {
  bq25896_config_t config = {};
  if (BQ25896_FAILED(bq25896_get_default_config(&config))) {
    return false;
  }
  if (BQ25896_FAILED(bq25896_hal_esp_idf_get_default_ctx(&g_charger_hal))) {
    return false;
  }
  g_charger_hal.scl_speed_hz = kI2cFrequencyHz;
  g_charger_hal.timeout_ms = 100;
  if (BQ25896_FAILED(bq25896_hal_esp_idf_ctx_init(
          &g_charger_hal, i2c_bus_handle(), kBq25896Address))) {
    return false;
  }
  if (BQ25896_FAILED(
          bq25896_hal_esp_idf_make_hal(&g_charger_hal, &config.hal))) {
    (void)bq25896_hal_esp_idf_ctx_deinit(&g_charger_hal);
    return false;
  }

  config.i2c_addr_7bit = kBq25896Address;
  config.reset_registers_on_init = true;
  config.exit_hiz_on_init = true;
  config.adc_mode = BQ25896_ADC_MODE_CONTINUOUS;
  config.watchdog = BQ25896_WATCHDOG_DISABLED;
  if (BQ25896_FAILED(bq25896_init(&g_charger, &config))) {
    (void)bq25896_hal_esp_idf_ctx_deinit(&g_charger_hal);
    g_charger = {};
    return false;
  }

  const bool ok =
      charger_call_ok(bq25896_disable_otg(&g_charger)) &&
      charger_call_ok(bq25896_enable_battery_power_path(&g_charger)) &&
      charger_call_ok(
          bq25896_set_input_limit_ma(&g_charger, kProfile.input_limit_ma)) &&
      charger_call_ok(
          bq25896_set_charge_current_ma(&g_charger, kProfile.charge_current_ma)) &&
      charger_call_ok(bq25896_set_precharge_current_ma(
          &g_charger, kProfile.precharge_current_ma)) &&
      charger_call_ok(bq25896_set_termination_current_ma(
          &g_charger, kProfile.termination_current_ma)) &&
      charger_call_ok(bq25896_set_charge_voltage_mv(
          &g_charger, kProfile.charge_voltage_mv)) &&
      charger_call_ok(bq25896_set_system_min_voltage_mv(
          &g_charger, kProfile.system_min_voltage_mv)) &&
      charger_call_ok(bq25896_enable_charge(&g_charger));
  if (!ok) {
    (void)bq25896_hal_esp_idf_ctx_deinit(&g_charger_hal);
    g_charger = {};
    return false;
  }

  ESP_LOGI(
      kTag,
      "BQ25896 configured input=%u mA charge=%u mA pre=%u mA term=%u mA "
      "vreg=%u mV sysmin=%u mV watchdog=off",
      kProfile.input_limit_ma,
      kProfile.charge_current_ma,
      kProfile.precharge_current_ma,
      kProfile.termination_current_ma,
      kProfile.charge_voltage_mv,
      kProfile.system_min_voltage_mv);
  return true;
}

bool configure_gauge() {
  if (!g_gauge.begin(i2c_bus_handle(), kBq27220Address, kI2cFrequencyHz)) {
    return false;
  }
  if (!g_gauge.setDefaultCapacity(kProfile.capacity_mah) ||
      !g_gauge.setChargeParameters(
          kProfile.charge_current_ma,
          kProfile.charge_voltage_mv,
          kProfile.termination_current_ma,
          kProfile.charge_termination_voltage_delta_mv) ||
      !g_gauge.init()) {
    g_gauge.end();
    return false;
  }
  ESP_LOGI(
      kTag,
      "BQ27220 configured capacity=%u mAh charge=%u mA/%u mV taper=%u mA",
      kProfile.capacity_mah,
      kProfile.charge_current_ma,
      kProfile.charge_voltage_mv,
      kProfile.termination_current_ma);
  return true;
}

bool charger_fault_active(const bq25896_fault_t &fault) {
  return fault.watchdog_fault || fault.boost_fault || fault.battery_fault ||
      fault.charge_fault != BQ25896_CHARGE_FAULT_NORMAL ||
      fault.ntc_fault != BQ25896_NTC_FAULT_NORMAL;
}

bool charger_config_matches_profile(const bq25896_charge_config_t &config) {
  return config.charge_enabled && !config.otg_enabled && !config.hiz_enabled &&
      !config.batfet_disabled &&
      config.charge_current_ma == kProfile.charge_current_ma &&
      config.precharge_current_ma == kProfile.precharge_current_ma &&
      config.termination_current_ma == kProfile.termination_current_ma &&
      config.charge_voltage_mv == kProfile.charge_voltage_mv &&
      config.sys_min_voltage_mv == kProfile.system_min_voltage_mv;
}

bool restore_charger_profile() {
  return charger_call_ok(bq25896_disable_otg(&g_charger)) &&
      charger_call_ok(bq25896_enable_battery_power_path(&g_charger)) &&
      charger_call_ok(
          bq25896_set_input_limit_ma(&g_charger, kProfile.input_limit_ma)) &&
      charger_call_ok(
          bq25896_set_charge_current_ma(&g_charger, kProfile.charge_current_ma)) &&
      charger_call_ok(bq25896_set_precharge_current_ma(
          &g_charger, kProfile.precharge_current_ma)) &&
      charger_call_ok(bq25896_set_termination_current_ma(
          &g_charger, kProfile.termination_current_ma)) &&
      charger_call_ok(bq25896_set_charge_voltage_mv(
          &g_charger, kProfile.charge_voltage_mv)) &&
      charger_call_ok(bq25896_set_system_min_voltage_mv(
          &g_charger, kProfile.system_min_voltage_mv)) &&
      charger_call_ok(bq25896_enable_charge(&g_charger));
}

}  // namespace

bool battery_begin() {
  if (g_battery_init_attempted) {
    return g_charger_ready || g_gauge_ready;
  }
  g_battery_init_attempted = true;
  g_charger_found = probe(kBq25896Address);
  g_gauge_found = probe(kBq27220Address);

  g_charger_ready = g_charger_found && configure_charger();
  g_gauge_ready = g_gauge_found && configure_gauge();
  ESP_LOGI(
      kTag,
      "battery management charger=%s gauge=%s",
      g_charger_ready ? "ready" : (g_charger_found ? "init-failed" : "missing"),
      g_gauge_ready ? "ready" : (g_gauge_found ? "init-failed" : "missing"));
  return g_charger_ready || g_gauge_ready;
}

void battery_service() {
  if (!g_battery_init_attempted) {
    (void)battery_begin();
  }

  const uint32_t now = millis();
  if ((now - g_last_charger_service_ms) < kChargerServicePeriodMs) {
    return;
  }
  g_last_charger_service_ms = now;

  if (!g_charger_ready) {
    g_charger_found = probe(kBq25896Address);
    if (g_charger_found) {
      g_charger_ready = configure_charger();
      ESP_LOGI(kTag, "BQ25896 retry %s", g_charger_ready ? "succeeded" : "failed");
    }
    return;
  }

  bq25896_fault_t fault = {};
  bq25896_charge_config_t config = {};
  const bool fault_ok =
      BQ25896_SUCCEEDED(bq25896_read_fault(&g_charger, &fault));
  const bool config_ok =
      BQ25896_SUCCEEDED(bq25896_read_charge_config(&g_charger, &config));
  if (!fault_ok || !config_ok) {
    ESP_LOGW(kTag, "charger service read failed; leaving safety state unchanged");
    return;
  }
  if (charger_fault_active(fault)) {
    ESP_LOGW(
        kTag,
        "charger safety fault raw=0x%02X charge=%u ntc=%u; not forcing charge",
        fault.raw_reg0c,
        static_cast<unsigned>(fault.charge_fault),
        static_cast<unsigned>(fault.ntc_fault));
    return;
  }
  if (!charger_config_matches_profile(config)) {
    const bool restored = restore_charger_profile();
    ESP_LOGW(
        kTag,
        "charger profile drift chg=%u otg=%u hiz=%u batfet=%u; restore=%s",
        config.charge_enabled ? 1U : 0U,
        config.otg_enabled ? 1U : 0U,
        config.hiz_enabled ? 1U : 0U,
        config.batfet_disabled ? 1U : 0U,
        restored ? "ok" : "failed");
  }
}

bool battery_read_status(PaperboyBatteryStatus &status) {
  (void)battery_begin();
  status = {};
  status.gauge_found = g_gauge_found;
  status.charger_found = g_charger_found;
  status.configured_input_limit_ma = kProfile.input_limit_ma;
  status.configured_charge_current_ma = kProfile.charge_current_ma;
  status.configured_precharge_current_ma = kProfile.precharge_current_ma;
  status.configured_termination_current_ma = kProfile.termination_current_ma;
  status.configured_charge_voltage_mv = kProfile.charge_voltage_mv;
  status.configured_system_min_voltage_mv = kProfile.system_min_voltage_mv;

  if (g_charger_ready) {
    bq25896_status_t charger_status = {};
    bq25896_adc_t charger_adc = {};
    bq25896_charge_config_t charger_config = {};
    bq25896_fault_t charger_fault = {};
    const bool status_ok = BQ25896_SUCCEEDED(
        bq25896_read_status(&g_charger, &charger_status));
    const bool adc_ok =
        BQ25896_SUCCEEDED(bq25896_read_adc(&g_charger, &charger_adc));
    const bool config_ok = BQ25896_SUCCEEDED(
        bq25896_read_charge_config(&g_charger, &charger_config));
    status.charger_fault_read_ok = BQ25896_SUCCEEDED(
        bq25896_read_fault(&g_charger, &charger_fault));
    status.charger_read_ok = status_ok && adc_ok && config_ok;

    if (status_ok) {
      status.usb_connected = charger_status.vbus_good || charger_status.power_good;
      status.charge_status = static_cast<uint8_t>(charger_status.charge_status);
      status.vbus_status = static_cast<uint8_t>(charger_status.vbus_status);
      status.active_input_limit_ma = charger_status.input_limit_ma;
      status.charge_done =
          charger_status.charge_status == BQ25896_CHARGE_STATUS_TERMINATION_DONE;
    }
    if (adc_ok) {
      status.usb_connected = status.usb_connected || charger_adc.vbus_good;
      status.thermal_regulation_active = charger_adc.thermal_regulation_active;
      status.charger_adc_current_ma = charger_adc.charge_current_ma;
      status.charger_battery_voltage_mv = charger_adc.battery_voltage_mv;
      status.system_voltage_mv = charger_adc.system_voltage_mv;
      status.vbus_voltage_mv = charger_adc.vbus_voltage_mv;
    }
    if (config_ok) {
      status.charge_enabled = charger_config.charge_enabled;
      status.hiz_enabled = charger_config.hiz_enabled;
      status.batfet_disabled = charger_config.batfet_disabled;
      status.otg_enabled = charger_config.otg_enabled;
      status.configured_charge_current_ma = charger_config.charge_current_ma;
      status.configured_precharge_current_ma = charger_config.precharge_current_ma;
      status.configured_termination_current_ma =
          charger_config.termination_current_ma;
      status.configured_charge_voltage_mv = charger_config.charge_voltage_mv;
      status.configured_system_min_voltage_mv = charger_config.sys_min_voltage_mv;
    }
    if (status.charger_fault_read_ok) {
      status.watchdog_fault = charger_fault.watchdog_fault;
      status.boost_fault = charger_fault.boost_fault;
      status.battery_fault = charger_fault.battery_fault;
      status.charge_fault = static_cast<uint8_t>(charger_fault.charge_fault);
      status.ntc_fault = static_cast<uint8_t>(charger_fault.ntc_fault);
      status.fault_present = charger_fault_active(charger_fault);
    }

    status.charging = status.charge_enabled &&
        (status.charge_status == BQ25896_CHARGE_STATUS_PRECHARGE ||
         status.charge_status == BQ25896_CHARGE_STATUS_FAST_CHARGE);
    if (status.voltage_mv == 0U) {
      status.voltage_mv = status.charger_battery_voltage_mv;
    }
  }

  if (g_gauge_ready) {
    BQ27220Snapshot gauge = {};
    status.gauge_read_ok = g_gauge.readSnapshot(&gauge);
    if (status.gauge_read_ok) {
      const bool inferred_vbus = status.usb_connected || gauge.charging;
      const BQ27220State gauge_state = BQ27220::classifyState(
          &gauge, inferred_vbus, kProfile.current_threshold_ma);
      status.soc_percent = gauge.soc;
      status.voltage_mv = gauge.voltage_mv;
      status.current_ma = gauge.current_ma;
      status.average_current_ma = gauge.average_current_ma;
      status.remaining_capacity_mah = gauge.remaining_capacity_mah;
      status.full_capacity_mah = gauge.fcc_mah;
      status.health_percent = gauge.soh_percent;
      status.temperature_dk = gauge.temperature_dk;
      status.cycle_count = g_gauge.readRegU16(CommandCycleCount);
      status.charging = status.charging || gauge_state == BQ27220StateCharge;
      status.charge_done = status.charge_done || gauge.full ||
          gauge.battery_status.reg.TCA || gauge.soc >= 100U;
      if (!status.charger_read_ok) {
        status.usb_connected = inferred_vbus;
      }
    }
  }

  return status.gauge_read_ok || status.charger_read_ok;
}

BatteryShutdownResult battery_request_shutdown() {
  (void)battery_begin();
  if (!g_charger_ready) {
    return BatteryShutdownResult::ChargerUnavailable;
  }

  bq25896_status_t status = {};
  if (BQ25896_FAILED(bq25896_read_status(&g_charger, &status))) {
    return BatteryShutdownResult::IoError;
  }
  if (status.vbus_good || status.power_good) {
    return BatteryShutdownResult::UsbConnected;
  }
  return BQ25896_SUCCEEDED(bq25896_shutdown(&g_charger))
      ? BatteryShutdownResult::PowerCutRequested
      : BatteryShutdownResult::IoError;
}
