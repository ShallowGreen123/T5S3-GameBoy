#include "battery_power.h"

#include <Arduino.h>
#include <Wire.h>

namespace {

constexpr uint8_t kBq27220Address = 0x55;
constexpr uint8_t kBq25896Address = 0x6B;

constexpr uint8_t kGaugeTemperature = 0x06;
constexpr uint8_t kGaugeVoltage = 0x08;
constexpr uint8_t kGaugeCurrent = 0x0C;
constexpr uint8_t kGaugeRemainingCapacity = 0x10;
constexpr uint8_t kGaugeFullChargeCapacity = 0x12;
constexpr uint8_t kGaugeAverageCurrent = 0x14;
constexpr uint8_t kGaugeCycleCount = 0x2A;
constexpr uint8_t kGaugeStateOfCharge = 0x2C;
constexpr uint8_t kGaugeStateOfHealth = 0x2E;

constexpr uint8_t kChargerPowerConfig = 0x03;
constexpr uint8_t kChargerMiscControl = 0x09;
constexpr uint8_t kChargerStatus = 0x0B;
constexpr uint8_t kChargerBatteryAdc = 0x0E;
constexpr uint8_t kChargerVbusAdc = 0x11;

constexpr uint8_t kChargerOtgMask = 1U << 5;
constexpr uint8_t kChargerChargeMask = 1U << 4;
constexpr uint8_t kChargerBatfetDisableMask = 1U << 5;
constexpr uint8_t kChargerChargeStatusMask = 0x18;
constexpr uint8_t kChargerPowerGoodMask = 1U << 2;
constexpr uint8_t kChargerVbusGoodMask = 1U << 7;

bool probe(uint8_t address) {
  Wire.beginTransmission(address);
  return Wire.endTransmission() == 0;
}

bool read_reg8(uint8_t address, uint8_t reg, uint8_t &value) {
  Wire.beginTransmission(address);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) {
    return false;
  }
  if (Wire.requestFrom(static_cast<int>(address), 1) != 1) {
    return false;
  }
  value = static_cast<uint8_t>(Wire.read());
  return true;
}

bool write_reg8(uint8_t address, uint8_t reg, uint8_t value) {
  Wire.beginTransmission(address);
  Wire.write(reg);
  Wire.write(value);
  return Wire.endTransmission() == 0;
}

bool read_reg16_le(uint8_t address, uint8_t reg, uint16_t &value) {
  Wire.beginTransmission(address);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) {
    return false;
  }
  if (Wire.requestFrom(static_cast<int>(address), 2) != 2) {
    return false;
  }
  const uint8_t low = static_cast<uint8_t>(Wire.read());
  const uint8_t high = static_cast<uint8_t>(Wire.read());
  value = static_cast<uint16_t>(low) | (static_cast<uint16_t>(high) << 8U);
  return true;
}

}  // namespace

bool battery_read_status(BatteryStatus &status) {
  status = {};
  status.gauge_found = probe(kBq27220Address);
  status.charger_found = probe(kBq25896Address);

  if (status.gauge_found) {
    uint16_t current_raw = 0;
    uint16_t average_current_raw = 0;
    uint16_t health_raw = 0;
    status.gauge_read_ok =
        read_reg16_le(kBq27220Address, kGaugeStateOfCharge, status.soc_percent) &&
        read_reg16_le(kBq27220Address, kGaugeVoltage, status.voltage_mv) &&
        read_reg16_le(kBq27220Address, kGaugeCurrent, current_raw) &&
        read_reg16_le(kBq27220Address, kGaugeAverageCurrent, average_current_raw) &&
        read_reg16_le(kBq27220Address, kGaugeRemainingCapacity, status.remaining_capacity_mah) &&
        read_reg16_le(kBq27220Address, kGaugeFullChargeCapacity, status.full_capacity_mah) &&
        read_reg16_le(kBq27220Address, kGaugeStateOfHealth, health_raw) &&
        read_reg16_le(kBq27220Address, kGaugeTemperature, status.temperature_dk) &&
        read_reg16_le(kBq27220Address, kGaugeCycleCount, status.cycle_count);
    status.current_ma = static_cast<int16_t>(current_raw);
    status.average_current_ma = static_cast<int16_t>(average_current_raw);
    status.health_percent = static_cast<uint16_t>(health_raw & 0x00FFU);
  }

  if (status.charger_found) {
    uint8_t charger_status = 0;
    uint8_t vbus_adc = 0;
    uint8_t battery_adc = 0;
    status.charger_read_ok =
        read_reg8(kBq25896Address, kChargerStatus, charger_status) &&
        read_reg8(kBq25896Address, kChargerVbusAdc, vbus_adc) &&
        read_reg8(kBq25896Address, kChargerBatteryAdc, battery_adc);
    if (status.charger_read_ok) {
      status.usb_connected =
          (charger_status & kChargerPowerGoodMask) != 0U ||
          (vbus_adc & kChargerVbusGoodMask) != 0U;
      const uint8_t charge_state =
          static_cast<uint8_t>((charger_status & kChargerChargeStatusMask) >> 3U);
      status.charging = charge_state == 1U || charge_state == 2U;
      status.charge_done = charge_state == 3U;
      if (!status.gauge_read_ok) {
        status.voltage_mv = static_cast<uint16_t>(
            2304U + (static_cast<uint16_t>(battery_adc & 0x7FU) * 20U));
      }
    }
  }

  if (status.gauge_read_ok) {
    status.charging = status.charging ||
        (status.soc_percent < 100U && status.average_current_ma > 20);
    status.charge_done = status.charge_done || status.soc_percent >= 100U;
  }
  return status.gauge_read_ok || status.charger_read_ok;
}

BatteryShutdownResult battery_request_shutdown() {
  if (!probe(kBq25896Address)) {
    return BatteryShutdownResult::ChargerUnavailable;
  }

  uint8_t status = 0;
  uint8_t vbus = 0;
  if (!read_reg8(kBq25896Address, kChargerStatus, status) ||
      !read_reg8(kBq25896Address, kChargerVbusAdc, vbus)) {
    return BatteryShutdownResult::IoError;
  }
  if ((status & kChargerPowerGoodMask) != 0U ||
      (vbus & kChargerVbusGoodMask) != 0U) {
    return BatteryShutdownResult::UsbConnected;
  }

  uint8_t power_config = 0;
  if (!read_reg8(kBq25896Address, kChargerPowerConfig, power_config)) {
    return BatteryShutdownResult::IoError;
  }
  power_config &= static_cast<uint8_t>(~(kChargerOtgMask | kChargerChargeMask));
  if (!write_reg8(kBq25896Address, kChargerPowerConfig, power_config)) {
    return BatteryShutdownResult::IoError;
  }

  uint8_t misc_control = 0;
  if (!read_reg8(kBq25896Address, kChargerMiscControl, misc_control)) {
    return BatteryShutdownResult::IoError;
  }
  misc_control |= kChargerBatfetDisableMask;
  if (!write_reg8(kBq25896Address, kChargerMiscControl, misc_control)) {
    return BatteryShutdownResult::IoError;
  }
  return BatteryShutdownResult::PowerCutRequested;
}
