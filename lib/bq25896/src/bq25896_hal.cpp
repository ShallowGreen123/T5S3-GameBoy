#include "bq25896_hal.h"

bq25896_err_t bq25896_hal_validate(const bq25896_hal_t *hal)
{
    if (hal == NULL)
    {
        return BQ25896_ERR_NULL;
    }

    if ((hal->i2c_read == NULL) || (hal->i2c_write == NULL))
    {
        return BQ25896_ERR_UNSUPPORTED;
    }

    return BQ25896_OK;
}

bq25896_err_t bq25896_hal_read(const bq25896_hal_t *hal,
                               uint8_t i2c_addr_7bit,
                               uint8_t reg,
                               uint8_t *data,
                               size_t len)
{
    bq25896_err_t status = bq25896_hal_validate(hal);

    if (BQ25896_FAILED(status))
    {
        return status;
    }

    if ((data == NULL) || (len == 0u))
    {
        return BQ25896_ERR_INVALID_ARG;
    }

    return hal->i2c_read(hal->user_ctx, i2c_addr_7bit, reg, data, len);
}

bq25896_err_t bq25896_hal_write(const bq25896_hal_t *hal,
                                uint8_t i2c_addr_7bit,
                                uint8_t reg,
                                const uint8_t *data,
                                size_t len)
{
    bq25896_err_t status = bq25896_hal_validate(hal);

    if (BQ25896_FAILED(status))
    {
        return status;
    }

    if ((data == NULL) || (len == 0u))
    {
        return BQ25896_ERR_INVALID_ARG;
    }

    return hal->i2c_write(hal->user_ctx, i2c_addr_7bit, reg, data, len);
}

bq25896_err_t bq25896_hal_delay(const bq25896_hal_t *hal, uint32_t delay_ms)
{
    bq25896_err_t status = bq25896_hal_validate(hal);

    if (BQ25896_FAILED(status))
    {
        return status;
    }

    if (hal->delay_ms == NULL)
    {
        return BQ25896_ERR_UNSUPPORTED;
    }

    hal->delay_ms(hal->user_ctx, delay_ms);
    return BQ25896_OK;
}
