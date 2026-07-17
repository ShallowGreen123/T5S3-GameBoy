#pragma once

#include <stdbool.h>
#include <stdint.h>

struct BatteryStatus {
  bool gauge_found = false;
  bool gauge_read_ok = false;
  bool charger_found = false;
  bool charger_read_ok = false;
  bool usb_connected = false;
  bool charging = false;
  bool charge_done = false;
  uint16_t soc_percent = 0;
  uint16_t voltage_mv = 0;
  int16_t current_ma = 0;
  int16_t average_current_ma = 0;
  uint16_t remaining_capacity_mah = 0;
  uint16_t full_capacity_mah = 0;
  uint16_t health_percent = 0;
  uint16_t temperature_dk = 0;
  uint16_t cycle_count = 0;
};

enum class BatteryShutdownResult : uint8_t {
  PowerCutRequested,
  UsbConnected,
  ChargerUnavailable,
  IoError,
};

bool battery_read_status(BatteryStatus &status);
BatteryShutdownResult battery_request_shutdown();
