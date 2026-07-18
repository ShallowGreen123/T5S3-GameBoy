#pragma once

// Arduino-ESP32 2.x does not provide ESP-IDF's newer i2c_master API.  The
// bundled BQ25896/BQ27220 drivers use that API, so map the small subset they
// need onto the already initialized Arduino Wire bus.

#include <Arduino.h>
#include <Wire.h>
#include <esp_err.h>

#include <cstddef>
#include <cstdint>
#include <new>

typedef void *i2c_master_bus_handle_t;

struct i2c_master_dev_t {
  TwoWire *wire = nullptr;
  uint8_t address = 0;
  uint32_t scl_speed_hz = 400000;
};

typedef i2c_master_dev_t *i2c_master_dev_handle_t;

typedef enum {
  I2C_ADDR_BIT_LEN_7 = 0,
} i2c_addr_bit_len_t;

typedef struct {
  i2c_addr_bit_len_t dev_addr_length;
  uint16_t device_address;
  uint32_t scl_speed_hz;
  uint32_t scl_wait_us;
} i2c_device_config_t;

typedef struct {
  uint8_t *write_buffer;
  size_t buffer_size;
} i2c_master_transmit_multi_buffer_info_t;

static inline TwoWire *i2c_master_compat_wire(i2c_master_bus_handle_t bus_handle) {
  return bus_handle == nullptr ? &Wire : reinterpret_cast<TwoWire *>(bus_handle);
}

static inline esp_err_t i2c_master_bus_add_device(
    i2c_master_bus_handle_t bus_handle,
    const i2c_device_config_t *dev_config,
    i2c_master_dev_handle_t *ret_handle) {
  if (dev_config == nullptr || ret_handle == nullptr ||
      dev_config->device_address > 0x7FU) {
    return ESP_ERR_INVALID_ARG;
  }

  i2c_master_dev_t *dev = new (std::nothrow) i2c_master_dev_t();
  if (dev == nullptr) {
    return ESP_ERR_NO_MEM;
  }
  dev->wire = i2c_master_compat_wire(bus_handle);
  dev->address = static_cast<uint8_t>(dev_config->device_address);
  dev->scl_speed_hz =
      dev_config->scl_speed_hz == 0U ? 400000U : dev_config->scl_speed_hz;
  dev->wire->setClock(dev->scl_speed_hz);
  *ret_handle = dev;
  return ESP_OK;
}

static inline esp_err_t i2c_master_bus_rm_device(
    i2c_master_dev_handle_t dev_handle) {
  if (dev_handle == nullptr) {
    return ESP_ERR_INVALID_ARG;
  }
  delete dev_handle;
  return ESP_OK;
}

static inline esp_err_t i2c_master_transmit_receive(
    i2c_master_dev_handle_t dev_handle,
    const uint8_t *write_buffer,
    size_t write_size,
    uint8_t *read_buffer,
    size_t read_size,
    int timeout_ms) {
  (void)timeout_ms;
  if (dev_handle == nullptr || dev_handle->wire == nullptr ||
      read_buffer == nullptr || read_size == 0U || read_size > 255U) {
    return ESP_ERR_INVALID_ARG;
  }

  if (write_size > 0U) {
    if (write_buffer == nullptr) {
      return ESP_ERR_INVALID_ARG;
    }
    dev_handle->wire->beginTransmission(dev_handle->address);
    if (dev_handle->wire->write(write_buffer, write_size) != write_size ||
        dev_handle->wire->endTransmission(false) != 0) {
      return ESP_FAIL;
    }
  }

  const uint8_t requested = static_cast<uint8_t>(read_size);
  if (dev_handle->wire->requestFrom(dev_handle->address, requested) != requested) {
    while (dev_handle->wire->available()) {
      dev_handle->wire->read();
    }
    return ESP_FAIL;
  }
  for (size_t i = 0; i < read_size; ++i) {
    read_buffer[i] = static_cast<uint8_t>(dev_handle->wire->read());
  }
  return ESP_OK;
}

static inline esp_err_t i2c_master_multi_buffer_transmit(
    i2c_master_dev_handle_t dev_handle,
    const i2c_master_transmit_multi_buffer_info_t *buffers,
    size_t buffer_count,
    int timeout_ms) {
  (void)timeout_ms;
  if (dev_handle == nullptr || dev_handle->wire == nullptr ||
      buffers == nullptr || buffer_count == 0U) {
    return ESP_ERR_INVALID_ARG;
  }

  dev_handle->wire->beginTransmission(dev_handle->address);
  for (size_t i = 0; i < buffer_count; ++i) {
    if (buffers[i].buffer_size > 0U) {
      if (buffers[i].write_buffer == nullptr ||
          dev_handle->wire->write(
              buffers[i].write_buffer, buffers[i].buffer_size) !=
              buffers[i].buffer_size) {
        return ESP_ERR_INVALID_ARG;
      }
    }
  }
  return dev_handle->wire->endTransmission(true) == 0 ? ESP_OK : ESP_FAIL;
}
