#include "pca9535_min.h"

#include "t5s3_epd_pins.h"

bool Pca9535Min::begin(TwoWire &wire, uint8_t address) {
  wire_ = &wire;
  address_ = address;

  wire_->beginTransmission(address_);
  present_ = (wire_->endTransmission() == 0);
  return present_;
}

bool Pca9535Min::configureProbeDefaults() {
  if (!present_) {
    return false;
  }

  output0_ = 0x00;
  output1_ = 0x00;
  config0_ = 0x00;
  config1_ = t5s3_epd::kPcaMaskButton | t5s3_epd::kPcaMaskTpsPwrGood | t5s3_epd::kPcaMaskTpsInt;

  return writeRegister(kRegPolarity0, 0x00) &&
         writeRegister(kRegPolarity1, 0x00) &&
         writeRegister(kRegOutput0, output0_) &&
         writeRegister(kRegOutput1, output1_) &&
         writeRegister(kRegConfig0, config0_) &&
         writeRegister(kRegConfig1, config1_);
}

bool Pca9535Min::readState(uint8_t &config0, uint8_t &config1, uint8_t &output0, uint8_t &output1) {
  return readRegister(kRegConfig0, config0) &&
         readRegister(kRegConfig1, config1) &&
         readRegister(kRegOutput0, output0) &&
         readRegister(kRegOutput1, output1);
}

bool Pca9535Min::readInputs(uint8_t &input0, uint8_t &input1) {
  return readRegister(kRegInput0, input0) && readRegister(kRegInput1, input1);
}

bool Pca9535Min::readPowerGood(bool &power_good) {
  uint8_t input1 = 0;
  uint8_t input0 = 0;
  if (!readInputs(input0, input1)) {
    return false;
  }
  (void)input0;
  power_good = (input1 & t5s3_epd::kPcaMaskTpsPwrGood) != 0;
  return true;
}

bool Pca9535Min::setOutputMask(uint8_t port, uint8_t mask, bool high) {
  if (!present_) {
    return false;
  }

  uint8_t *cached = (port == 0) ? &output0_ : &output1_;
  *cached = high ? static_cast<uint8_t>(*cached | mask) : static_cast<uint8_t>(*cached & ~mask);
  return writeRegister(port == 0 ? kRegOutput0 : kRegOutput1, *cached);
}

bool Pca9535Min::safeShutdownOutputs() {
  if (!present_) {
    return false;
  }

  output1_ &= static_cast<uint8_t>(~t5s3_epd::kPcaMaskShutdownOutputs);
  return writeRegister(kRegOutput1, output1_);
}

bool Pca9535Min::writeRegister(uint8_t reg, uint8_t value) {
  wire_->beginTransmission(address_);
  wire_->write(reg);
  wire_->write(value);
  return wire_->endTransmission() == 0;
}

bool Pca9535Min::readRegister(uint8_t reg, uint8_t &value) {
  wire_->beginTransmission(address_);
  wire_->write(reg);
  if (wire_->endTransmission(false) != 0) {
    return false;
  }
  if (wire_->requestFrom(static_cast<int>(address_), 1) != 1) {
    return false;
  }
  value = wire_->read();
  return true;
}
