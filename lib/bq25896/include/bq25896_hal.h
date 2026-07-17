#ifndef BQ25896_HAL_H
#define BQ25896_HAL_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file bq25896_hal.h
 * @brief Platform abstraction hooks for the BQ25896 driver.
 */

/**
 * @brief Unified return codes used by the HAL and high-level driver APIs.
 */
typedef enum
{
    BQ25896_OK = 0,
    BQ25896_WARN_CLAMPED = 1,
    BQ25896_ERR_NULL = -1,
    BQ25896_ERR_INVALID_ARG = -2,
    BQ25896_ERR_I2C = -3,
    BQ25896_ERR_TIMEOUT = -4,
    BQ25896_ERR_DEVICE_ID = -5,
    BQ25896_ERR_UNSUPPORTED = -6,
    BQ25896_ERR_NOT_INITIALIZED = -7
} bq25896_err_t;

/**
 * @brief Returns true when a driver call completed without a fatal error.
 *
 * `BQ25896_WARN_CLAMPED` is treated as success so callers can distinguish
 * between "applied exactly" and "applied after range/step adjustment".
 */
#define BQ25896_SUCCEEDED(status) ((status) >= BQ25896_OK)

/**
 * @brief Returns true when a driver call failed.
 */
#define BQ25896_FAILED(status)    ((status) < BQ25896_OK)

/**
 * @brief User-supplied I2C register read callback.
 *
 * @param user_ctx User-defined context pointer.
 * @param i2c_addr_7bit BQ25896 7-bit I2C address.
 * @param reg Start register address.
 * @param data Destination buffer.
 * @param len Number of bytes to read.
 *
 * @return `BQ25896_OK` on success, otherwise a negative error code.
 */
typedef bq25896_err_t (*bq25896_hal_i2c_read_fn)(void *user_ctx,
                                                 uint8_t i2c_addr_7bit,
                                                 uint8_t reg,
                                                 uint8_t *data,
                                                 size_t len);

/**
 * @brief User-supplied I2C register write callback.
 *
 * @param user_ctx User-defined context pointer.
 * @param i2c_addr_7bit BQ25896 7-bit I2C address.
 * @param reg Start register address.
 * @param data Source buffer.
 * @param len Number of bytes to write.
 *
 * @return `BQ25896_OK` on success, otherwise a negative error code.
 */
typedef bq25896_err_t (*bq25896_hal_i2c_write_fn)(void *user_ctx,
                                                  uint8_t i2c_addr_7bit,
                                                  uint8_t reg,
                                                  const uint8_t *data,
                                                  size_t len);

/**
 * @brief Optional millisecond delay callback.
 *
 * This callback is used by `reset()` and one-shot ADC conversion polling.
 * If it is omitted, APIs that depend on timed polling return
 * `BQ25896_ERR_UNSUPPORTED`.
 *
 * @param user_ctx User-defined context pointer.
 * @param delay_ms Delay length in milliseconds.
 */
typedef void (*bq25896_hal_delay_ms_fn)(void *user_ctx, uint32_t delay_ms);

/**
 * @brief HAL function table and opaque context pointer.
 */
typedef struct
{
    bq25896_hal_i2c_read_fn i2c_read;   /**< User-provided I2C read function. */
    bq25896_hal_i2c_write_fn i2c_write; /**< User-provided I2C write function. */
    bq25896_hal_delay_ms_fn delay_ms;   /**< Optional delay function. */
    void *user_ctx;                     /**< Opaque user context passed back to callbacks. */
} bq25896_hal_t;

/**
 * @brief Validate a HAL descriptor.
 *
 * @param hal HAL descriptor to validate.
 *
 * @return `BQ25896_OK` when the required callbacks are present.
 */
bq25896_err_t bq25896_hal_validate(const bq25896_hal_t *hal);

/**
 * @brief Read one or more registers through the user-provided HAL.
 *
 * @param hal HAL descriptor.
 * @param i2c_addr_7bit BQ25896 7-bit I2C address.
 * @param reg Start register address.
 * @param data Destination buffer.
 * @param len Number of bytes to read.
 *
 * @return `BQ25896_OK` on success, otherwise a negative error code.
 */
bq25896_err_t bq25896_hal_read(const bq25896_hal_t *hal,
                               uint8_t i2c_addr_7bit,
                               uint8_t reg,
                               uint8_t *data,
                               size_t len);

/**
 * @brief Write one or more registers through the user-provided HAL.
 *
 * @param hal HAL descriptor.
 * @param i2c_addr_7bit BQ25896 7-bit I2C address.
 * @param reg Start register address.
 * @param data Source buffer.
 * @param len Number of bytes to write.
 *
 * @return `BQ25896_OK` on success, otherwise a negative error code.
 */
bq25896_err_t bq25896_hal_write(const bq25896_hal_t *hal,
                                uint8_t i2c_addr_7bit,
                                uint8_t reg,
                                const uint8_t *data,
                                size_t len);

/**
 * @brief Run the optional delay callback.
 *
 * @param hal HAL descriptor.
 * @param delay_ms Delay length in milliseconds.
 *
 * @return `BQ25896_OK` when the delay callback exists, otherwise
 *         `BQ25896_ERR_UNSUPPORTED`.
 */
bq25896_err_t bq25896_hal_delay(const bq25896_hal_t *hal, uint32_t delay_ms);

#ifdef __cplusplus
}
#endif

#endif /* BQ25896_HAL_H */
