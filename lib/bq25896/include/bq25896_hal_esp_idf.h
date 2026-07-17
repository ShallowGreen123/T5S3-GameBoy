#ifndef BQ25896_HAL_ESP_IDF_H
#define BQ25896_HAL_ESP_IDF_H

#include "driver/i2c_master.h"

#include "bq25896_hal.h"
#include "bq25896_reg.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file bq25896_hal_esp_idf.h
 * @brief ESP-IDF `i2c_master` adapter for the BQ25896 HAL callbacks.
 */

#define BQ25896_HAL_ESP_IDF_DEFAULT_SCL_SPEED_HZ  400000u
#define BQ25896_HAL_ESP_IDF_DEFAULT_SCL_WAIT_US   0u
#define BQ25896_HAL_ESP_IDF_DEFAULT_TIMEOUT_MS    100

/**
 * @brief ESP-IDF-backed HAL context.
 */
typedef struct
{
    i2c_master_bus_handle_t bus_handle;  /**< Source bus handle used to create the device handle. */
    i2c_master_dev_handle_t dev_handle;  /**< Device handle used by HAL callbacks. */
    uint8_t device_address;              /**< 7-bit I2C address bound to `dev_handle`. */
    uint32_t scl_speed_hz;               /**< Device SCL speed used at creation time. */
    uint32_t scl_wait_us;                /**< Optional clock-stretch wait in microseconds. */
    int timeout_ms;                      /**< Transfer timeout passed to ESP-IDF APIs. */
    bool owns_device_handle;             /**< True when the helper created `dev_handle`. */
} bq25896_hal_esp_idf_ctx_t;

/**
 * @brief Fill an ESP-IDF HAL context with safe defaults.
 *
 * @param ctx Output context pointer.
 *
 * @return `BQ25896_OK` on success.
 */
bq25896_err_t bq25896_hal_esp_idf_get_default_ctx(bq25896_hal_esp_idf_ctx_t *ctx);

/**
 * @brief Create an ESP-IDF I2C device handle for BQ25896 access.
 *
 * Call `bq25896_hal_esp_idf_get_default_ctx()` first if you want the default
 * speed and timeout, then override fields as needed before calling this API.
 *
 * @param ctx Context to initialize.
 * @param bus_handle ESP-IDF I2C master bus handle.
 * @param device_address 7-bit BQ25896 I2C address.
 *
 * @return `BQ25896_OK` on success, otherwise a negative error code.
 */
bq25896_err_t bq25896_hal_esp_idf_ctx_init(bq25896_hal_esp_idf_ctx_t *ctx,
                                           i2c_master_bus_handle_t bus_handle,
                                           uint8_t device_address);

/**
 * @brief Remove the ESP-IDF I2C device handle created by
 *        `bq25896_hal_esp_idf_ctx_init()`.
 *
 * @param ctx Context to deinitialize.
 *
 * @return `BQ25896_OK` on success, otherwise a negative error code.
 */
bq25896_err_t bq25896_hal_esp_idf_ctx_deinit(bq25896_hal_esp_idf_ctx_t *ctx);

/**
 * @brief Populate a generic `bq25896_hal_t` using the ESP-IDF helper callbacks.
 *
 * @param ctx Initialized ESP-IDF HAL context.
 * @param hal Output HAL descriptor.
 *
 * @return `BQ25896_OK` on success, otherwise a negative error code.
 */
bq25896_err_t bq25896_hal_esp_idf_make_hal(bq25896_hal_esp_idf_ctx_t *ctx, bq25896_hal_t *hal);

/**
 * @brief ESP-IDF-backed register read callback.
 *
 * @param user_ctx Pointer to `bq25896_hal_esp_idf_ctx_t`.
 * @param i2c_addr_7bit Target 7-bit I2C address.
 * @param reg Start register address.
 * @param data Read destination.
 * @param len Number of bytes to read.
 *
 * @return `BQ25896_OK` on success, otherwise a negative error code.
 */
bq25896_err_t bq25896_hal_esp_idf_read(void *user_ctx,
                                       uint8_t i2c_addr_7bit,
                                       uint8_t reg,
                                       uint8_t *data,
                                       size_t len);

/**
 * @brief ESP-IDF-backed register write callback.
 *
 * @param user_ctx Pointer to `bq25896_hal_esp_idf_ctx_t`.
 * @param i2c_addr_7bit Target 7-bit I2C address.
 * @param reg Start register address.
 * @param data Write source.
 * @param len Number of bytes to write.
 *
 * @return `BQ25896_OK` on success, otherwise a negative error code.
 */
bq25896_err_t bq25896_hal_esp_idf_write(void *user_ctx,
                                        uint8_t i2c_addr_7bit,
                                        uint8_t reg,
                                        const uint8_t *data,
                                        size_t len);

/**
 * @brief ESP-IDF-backed millisecond delay helper.
 *
 * This helper uses `esp_rom_delay_us()`, so it works before and after the
 * scheduler starts.
 *
 * @param user_ctx Unused.
 * @param delay_ms Delay length in milliseconds.
 */
void bq25896_hal_esp_idf_delay_ms(void *user_ctx, uint32_t delay_ms);

#ifdef __cplusplus
}
#endif

#endif /* BQ25896_HAL_ESP_IDF_H */
