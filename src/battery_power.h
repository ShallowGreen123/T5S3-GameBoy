#pragma once

#include <stdbool.h>
#include <stdint.h>

struct PaperboyBatteryStatus {
  bool gauge_found = false;
  bool gauge_read_ok = false;
  bool charger_found = false;
  bool charger_read_ok = false;
  bool charger_fault_read_ok = false;
  bool usb_connected = false;
  bool charging = false;
  bool charge_done = false;
  bool charge_enabled = false;
  bool hiz_enabled = false;
  bool batfet_disabled = false;
  bool otg_enabled = false;
  bool thermal_regulation_active = false;
  bool fault_present = false;
  bool watchdog_fault = false;
  bool boost_fault = false;
  bool battery_fault = false;
  uint8_t charge_status = 0;
  uint8_t vbus_status = 0;
  uint8_t charge_fault = 0;
  uint8_t ntc_fault = 0;
  uint16_t soc_percent = 0;
  uint16_t voltage_mv = 0;
  int16_t current_ma = 0;
  int16_t average_current_ma = 0;
  uint16_t remaining_capacity_mah = 0;
  uint16_t full_capacity_mah = 0;
  uint16_t health_percent = 0;
  uint16_t temperature_dk = 0;
  uint16_t cycle_count = 0;
  uint16_t configured_input_limit_ma = 0;
  uint16_t active_input_limit_ma = 0;
  uint16_t configured_charge_current_ma = 0;
  uint16_t configured_precharge_current_ma = 0;
  uint16_t configured_termination_current_ma = 0;
  uint16_t configured_charge_voltage_mv = 0;
  uint16_t configured_system_min_voltage_mv = 0;
  uint16_t charger_adc_current_ma = 0;
  uint16_t charger_battery_voltage_mv = 0;
  uint16_t system_voltage_mv = 0;
  uint16_t vbus_voltage_mv = 0;
};

enum class BatteryShutdownResult : uint8_t {
  PowerCutRequested,
  UsbConnected,
  ChargerUnavailable,
  IoError,
};

bool battery_begin();
void battery_service();
bool battery_read_status(PaperboyBatteryStatus &status);
BatteryShutdownResult battery_request_shutdown();
