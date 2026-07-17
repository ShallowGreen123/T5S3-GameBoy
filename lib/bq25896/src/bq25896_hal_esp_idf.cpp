#include "bq25896_hal_esp_idf.h"

#include <string.h>

#include "esp_err.h"
#include "esp_rom_sys.h"

namespace
{
static bq25896_err_t bq25896_hal_esp_idf_map_error(esp_err_t err)
{
    switch (err)
    {
        case ESP_OK:
            return BQ25896_OK;

        case ESP_ERR_INVALID_ARG:
            return BQ25896_ERR_INVALID_ARG;

        case ESP_ERR_TIMEOUT:
            return BQ25896_ERR_TIMEOUT;

        default:
            return BQ25896_ERR_I2C;
    }
}

static bq25896_err_t bq25896_hal_esp_idf_validate_ctx(const bq25896_hal_esp_idf_ctx_t *ctx)
{
    if (ctx == NULL)
    {
        return BQ25896_ERR_NULL;
    }

    if (ctx->device_address > BQ25896_I2C_ADDR_7BIT_MAX)
    {
        return BQ25896_ERR_INVALID_ARG;
    }

    if (ctx->timeout_ms < -1)
    {
        return BQ25896_ERR_INVALID_ARG;
    }

    if (ctx->dev_handle == NULL)
    {
        return BQ25896_ERR_NOT_INITIALIZED;
    }

    return BQ25896_OK;
}

static bq25896_err_t bq25896_hal_esp_idf_validate_transfer(const bq25896_hal_esp_idf_ctx_t *ctx,
                                                           uint8_t i2c_addr_7bit,
                                                           const void *buffer,
                                                           size_t len)
{
    bq25896_err_t status = bq25896_hal_esp_idf_validate_ctx(ctx);

    if (BQ25896_FAILED(status))
    {
        return status;
    }

    if ((buffer == NULL) || (len == 0u))
    {
        return BQ25896_ERR_INVALID_ARG;
    }

    if (i2c_addr_7bit != ctx->device_address)
    {
        return BQ25896_ERR_INVALID_ARG;
    }

    return BQ25896_OK;
}
} /* namespace */

extern "C"
{
bq25896_err_t bq25896_hal_esp_idf_get_default_ctx(bq25896_hal_esp_idf_ctx_t *ctx)
{
    if (ctx == NULL)
    {
        return BQ25896_ERR_NULL;
    }

    memset(ctx, 0, sizeof(*ctx));
    ctx->scl_speed_hz = BQ25896_HAL_ESP_IDF_DEFAULT_SCL_SPEED_HZ;
    ctx->scl_wait_us = BQ25896_HAL_ESP_IDF_DEFAULT_SCL_WAIT_US;
    ctx->timeout_ms = BQ25896_HAL_ESP_IDF_DEFAULT_TIMEOUT_MS;

    return BQ25896_OK;
}

bq25896_err_t bq25896_hal_esp_idf_ctx_init(bq25896_hal_esp_idf_ctx_t *ctx,
                                           i2c_master_bus_handle_t bus_handle,
                                           uint8_t device_address)
{
    i2c_device_config_t dev_cfg;
    esp_err_t err;

    if (ctx == NULL)
    {
        return BQ25896_ERR_NULL;
    }

    if ((bus_handle == NULL) || (device_address > BQ25896_I2C_ADDR_7BIT_MAX))
    {
        return BQ25896_ERR_INVALID_ARG;
    }

    if (ctx->dev_handle != NULL)
    {
        return BQ25896_ERR_INVALID_ARG;
    }

    if (ctx->scl_speed_hz == 0u)
    {
        ctx->scl_speed_hz = BQ25896_HAL_ESP_IDF_DEFAULT_SCL_SPEED_HZ;
    }

    if (ctx->timeout_ms == 0)
    {
        ctx->timeout_ms = BQ25896_HAL_ESP_IDF_DEFAULT_TIMEOUT_MS;
    }

    memset(&dev_cfg, 0, sizeof(dev_cfg));
    dev_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dev_cfg.device_address = device_address;
    dev_cfg.scl_speed_hz = ctx->scl_speed_hz;
    dev_cfg.scl_wait_us = ctx->scl_wait_us;

    err = i2c_master_bus_add_device(bus_handle, &dev_cfg, &ctx->dev_handle);
    if (err != ESP_OK)
    {
        return bq25896_hal_esp_idf_map_error(err);
    }

    ctx->bus_handle = bus_handle;
    ctx->device_address = device_address;
    ctx->owns_device_handle = true;

    return BQ25896_OK;
}

bq25896_err_t bq25896_hal_esp_idf_ctx_deinit(bq25896_hal_esp_idf_ctx_t *ctx)
{
    esp_err_t err;

    if (ctx == NULL)
    {
        return BQ25896_ERR_NULL;
    }

    if (ctx->dev_handle == NULL)
    {
        return BQ25896_ERR_NOT_INITIALIZED;
    }

    if (ctx->owns_device_handle)
    {
        err = i2c_master_bus_rm_device(ctx->dev_handle);
        if (err != ESP_OK)
        {
            return bq25896_hal_esp_idf_map_error(err);
        }
    }

    ctx->dev_handle = NULL;
    ctx->bus_handle = NULL;
    ctx->device_address = 0u;
    ctx->owns_device_handle = false;

    return BQ25896_OK;
}

bq25896_err_t bq25896_hal_esp_idf_make_hal(bq25896_hal_esp_idf_ctx_t *ctx, bq25896_hal_t *hal)
{
    bq25896_err_t status = bq25896_hal_esp_idf_validate_ctx(ctx);

    if (BQ25896_FAILED(status))
    {
        return status;
    }

    if (hal == NULL)
    {
        return BQ25896_ERR_NULL;
    }

    hal->i2c_read = bq25896_hal_esp_idf_read;
    hal->i2c_write = bq25896_hal_esp_idf_write;
    hal->delay_ms = bq25896_hal_esp_idf_delay_ms;
    hal->user_ctx = ctx;

    return BQ25896_OK;
}

bq25896_err_t bq25896_hal_esp_idf_read(void *user_ctx,
                                       uint8_t i2c_addr_7bit,
                                       uint8_t reg,
                                       uint8_t *data,
                                       size_t len)
{
    const bq25896_hal_esp_idf_ctx_t *ctx = (const bq25896_hal_esp_idf_ctx_t *)user_ctx;
    bq25896_err_t status = bq25896_hal_esp_idf_validate_transfer(ctx, i2c_addr_7bit, data, len);

    if (BQ25896_FAILED(status))
    {
        return status;
    }

    return bq25896_hal_esp_idf_map_error(i2c_master_transmit_receive(ctx->dev_handle,
                                                                      &reg,
                                                                      1u,
                                                                      data,
                                                                      len,
                                                                      ctx->timeout_ms));
}

bq25896_err_t bq25896_hal_esp_idf_write(void *user_ctx,
                                        uint8_t i2c_addr_7bit,
                                        uint8_t reg,
                                        const uint8_t *data,
                                        size_t len)
{
    const bq25896_hal_esp_idf_ctx_t *ctx = (const bq25896_hal_esp_idf_ctx_t *)user_ctx;
    i2c_master_transmit_multi_buffer_info_t buffers[2];
    uint8_t reg_buffer[1];
    bq25896_err_t status = bq25896_hal_esp_idf_validate_transfer(ctx, i2c_addr_7bit, data, len);

    if (BQ25896_FAILED(status))
    {
        return status;
    }

    reg_buffer[0] = reg;
    buffers[0].write_buffer = reg_buffer;
    buffers[0].buffer_size = sizeof(reg_buffer);
    buffers[1].write_buffer = (uint8_t *)data;
    buffers[1].buffer_size = len;

    return bq25896_hal_esp_idf_map_error(i2c_master_multi_buffer_transmit(ctx->dev_handle,
                                                                           buffers,
                                                                           2u,
                                                                           ctx->timeout_ms));
}

void bq25896_hal_esp_idf_delay_ms(void *user_ctx, uint32_t delay_ms)
{
    (void)user_ctx;

    while (delay_ms > 0u)
    {
        uint32_t chunk_ms = (delay_ms > 1000u) ? 1000u : delay_ms;
        esp_rom_delay_us(chunk_ms * 1000u);
        delay_ms -= chunk_ms;
    }
}
} /* extern "C" */
