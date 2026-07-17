#include "bq25896.h"

#include <string.h>

#define BQ25896_SINGLE_BYTE_LEN 1u
#define BQ25896_ADC_BLOCK_LEN   6u

typedef struct
{
    uint8_t reg_value;
    bq25896_err_t status;
} bq25896_quantized_value_t;

static bq25896_err_t bq25896_validate_device(const bq25896_t *dev)
{
    bq25896_err_t status;

    if (dev == NULL)
    {
        return BQ25896_ERR_NULL;
    }

    if (!dev->is_initialized)
    {
        return BQ25896_ERR_NOT_INITIALIZED;
    }

    if (dev->i2c_addr_7bit > BQ25896_I2C_ADDR_7BIT_MAX)
    {
        return BQ25896_ERR_INVALID_ARG;
    }

    status = bq25896_hal_validate(&dev->hal);
    if (BQ25896_FAILED(status))
    {
        return status;
    }

    return BQ25896_OK;
}

static bq25896_err_t bq25896_validate_init_config(const bq25896_config_t *config)
{
    bq25896_err_t status;

    if (config == NULL)
    {
        return BQ25896_ERR_NULL;
    }

    if (config->i2c_addr_7bit > BQ25896_I2C_ADDR_7BIT_MAX)
    {
        return BQ25896_ERR_INVALID_ARG;
    }

    status = bq25896_hal_validate(&config->hal);
    if (BQ25896_FAILED(status))
    {
        return status;
    }

    if ((config->adc_mode != BQ25896_ADC_MODE_ONE_SHOT) &&
        (config->adc_mode != BQ25896_ADC_MODE_CONTINUOUS))
    {
        return BQ25896_ERR_INVALID_ARG;
    }

    if ((config->watchdog != BQ25896_WATCHDOG_DISABLED) &&
        (config->watchdog != BQ25896_WATCHDOG_40S) &&
        (config->watchdog != BQ25896_WATCHDOG_80S) &&
        (config->watchdog != BQ25896_WATCHDOG_160S))
    {
        return BQ25896_ERR_INVALID_ARG;
    }

    return BQ25896_OK;
}

static bq25896_err_t bq25896_read_reg(const bq25896_t *dev, uint8_t reg, uint8_t *value)
{
    bq25896_err_t status = bq25896_validate_device(dev);

    if (BQ25896_FAILED(status))
    {
        return status;
    }

    if (value == NULL)
    {
        return BQ25896_ERR_NULL;
    }

    return bq25896_hal_read(&dev->hal,
                            dev->i2c_addr_7bit,
                            reg,
                            value,
                            BQ25896_SINGLE_BYTE_LEN);
}

static bq25896_err_t bq25896_read_block(const bq25896_t *dev,
                                        uint8_t start_reg,
                                        uint8_t *data,
                                        size_t len)
{
    bq25896_err_t status = bq25896_validate_device(dev);

    if (BQ25896_FAILED(status))
    {
        return status;
    }

    if ((data == NULL) || (len == 0u))
    {
        return BQ25896_ERR_INVALID_ARG;
    }

    return bq25896_hal_read(&dev->hal, dev->i2c_addr_7bit, start_reg, data, len);
}

static bq25896_err_t bq25896_update_bits(const bq25896_t *dev,
                                         uint8_t reg,
                                         uint8_t mask,
                                         uint8_t value)
{
    uint8_t reg_value = 0u;
    uint8_t new_value = 0u;
    bq25896_err_t status = bq25896_read_reg(dev, reg, &reg_value);

    if (BQ25896_FAILED(status))
    {
        return status;
    }

    new_value = (uint8_t)((reg_value & (uint8_t)(~mask)) | (value & mask));

    if (new_value == reg_value)
    {
        return BQ25896_OK;
    }

    return bq25896_hal_write(&dev->hal,
                             dev->i2c_addr_7bit,
                             reg,
                             &new_value,
                             BQ25896_SINGLE_BYTE_LEN);
}

static bq25896_err_t bq25896_wait_for_bit_clear(const bq25896_t *dev,
                                                uint8_t reg,
                                                uint8_t mask,
                                                uint32_t poll_count,
                                                uint32_t poll_delay_ms)
{
    uint8_t value = 0u;
    uint32_t attempt = 0u;

    for (attempt = 0u; attempt < poll_count; ++attempt)
    {
        bq25896_err_t status = bq25896_read_reg(dev, reg, &value);
        if (BQ25896_FAILED(status))
        {
            return status;
        }

        if ((value & mask) == 0u)
        {
            return BQ25896_OK;
        }

        if (attempt + 1u < poll_count)
        {
            status = bq25896_hal_delay(&dev->hal, poll_delay_ms);
            if (BQ25896_FAILED(status))
            {
                return status;
            }
        }
    }

    return BQ25896_ERR_TIMEOUT;
}

static bq25896_quantized_value_t bq25896_quantize_u16(uint16_t requested,
                                                      uint16_t min_value,
                                                      uint16_t max_value,
                                                      uint16_t offset,
                                                      uint16_t step,
                                                      uint8_t raw_max)
{
    bq25896_quantized_value_t result;
    uint16_t clamped = requested;
    uint16_t quantized = 0u;
    uint16_t raw_value = 0u;

    result.reg_value = 0u;
    result.status = BQ25896_OK;

    if (requested < min_value)
    {
        clamped = min_value;
        result.status = BQ25896_WARN_CLAMPED;
    }
    else if (requested > max_value)
    {
        clamped = max_value;
        result.status = BQ25896_WARN_CLAMPED;
    }

    quantized = (uint16_t)(offset + (((clamped - offset) / step) * step));
    raw_value = (uint16_t)((quantized - offset) / step);

    if ((quantized != requested) || (raw_value > raw_max))
    {
        result.status = BQ25896_WARN_CLAMPED;
    }

    if (raw_value > raw_max)
    {
        raw_value = raw_max;
    }

    result.reg_value = (uint8_t)raw_value;
    return result;
}

static uint16_t bq25896_decode_u16(uint8_t raw, uint8_t mask, uint8_t shift, uint16_t offset, uint16_t step)
{
    return (uint16_t)(offset + (uint16_t)(BQ25896_FIELD_GET(mask, shift, raw) * step));
}

static uint32_t bq25896_decode_u32(uint8_t raw, uint8_t mask, uint8_t shift, uint32_t offset, uint32_t step)
{
    return offset + (uint32_t)(BQ25896_FIELD_GET(mask, shift, raw) * step);
}

static bq25896_err_t bq25896_apply_init_config(const bq25896_t *dev, const bq25896_config_t *config)
{
    bq25896_err_t status;

    status = bq25896_update_bits(dev,
                                 BQ25896_REG_00,
                                 BQ25896_REG00_EN_ILIM_MASK,
                                 (uint8_t)BQ25896_FIELD_PREP(BQ25896_REG00_EN_ILIM_MASK,
                                                             BQ25896_REG00_EN_ILIM_SHIFT,
                                                             config->enable_ilim_pin ? 1u : 0u));
    if (BQ25896_FAILED(status))
    {
        return status;
    }

    if (config->exit_hiz_on_init)
    {
        status = bq25896_update_bits(dev,
                                     BQ25896_REG_00,
                                     BQ25896_REG00_EN_HIZ_MASK,
                                     (uint8_t)BQ25896_FIELD_PREP(BQ25896_REG00_EN_HIZ_MASK,
                                                                 BQ25896_REG00_EN_HIZ_SHIFT,
                                                                 0u));
        if (BQ25896_FAILED(status))
        {
            return status;
        }
    }

    status = bq25896_update_bits(dev,
                                 BQ25896_REG_02,
                                 BQ25896_REG02_ICO_EN_MASK,
                                 (uint8_t)BQ25896_FIELD_PREP(BQ25896_REG02_ICO_EN_MASK,
                                                             BQ25896_REG02_ICO_EN_SHIFT,
                                                             config->enable_ico ? 1u : 0u));
    if (BQ25896_FAILED(status))
    {
        return status;
    }

    status = bq25896_update_bits(dev,
                                 BQ25896_REG_02,
                                 BQ25896_REG02_CONV_RATE_MASK,
                                 (uint8_t)BQ25896_FIELD_PREP(BQ25896_REG02_CONV_RATE_MASK,
                                                             BQ25896_REG02_CONV_RATE_SHIFT,
                                                             (uint8_t)config->adc_mode));
    if (BQ25896_FAILED(status))
    {
        return status;
    }

    status = bq25896_update_bits(dev,
                                 BQ25896_REG_07,
                                 BQ25896_REG07_WATCHDOG_MASK,
                                 (uint8_t)BQ25896_FIELD_PREP(BQ25896_REG07_WATCHDOG_MASK,
                                                             BQ25896_REG07_WATCHDOG_SHIFT,
                                                             (uint8_t)config->watchdog));
    if (BQ25896_FAILED(status))
    {
        return status;
    }

    return BQ25896_OK;
}

static bq25896_err_t bq25896_verify_device_id(const bq25896_t *dev)
{
    uint8_t reg14 = 0u;
    bq25896_err_t status = bq25896_read_reg(dev, BQ25896_REG_14, &reg14);

    if (BQ25896_FAILED(status))
    {
        return status;
    }

    if (BQ25896_FIELD_GET(BQ25896_REG14_PN_MASK, BQ25896_REG14_PN_SHIFT, reg14) != BQ25896_DEVICE_PN_BQ25896)
    {
        return BQ25896_ERR_DEVICE_ID;
    }

    return BQ25896_OK;
}

bq25896_err_t bq25896_get_default_config(bq25896_config_t *config)
{
    if (config == NULL)
    {
        return BQ25896_ERR_NULL;
    }

    memset(config, 0, sizeof(*config));
    config->i2c_addr_7bit = BQ25896_I2C_ADDR_7BIT_DEFAULT;
    config->enable_ilim_pin = true;
    config->enable_ico = true;
    config->adc_mode = BQ25896_ADC_MODE_CONTINUOUS;
    config->watchdog = BQ25896_WATCHDOG_40S;

    return BQ25896_OK;
}

bq25896_err_t bq25896_init(bq25896_t *dev, const bq25896_config_t *config)
{
    bq25896_err_t status;

    if (dev == NULL)
    {
        return BQ25896_ERR_NULL;
    }

    status = bq25896_validate_init_config(config);
    if (BQ25896_FAILED(status))
    {
        return status;
    }

    memset(dev, 0, sizeof(*dev));
    dev->hal = config->hal;
    dev->i2c_addr_7bit = config->i2c_addr_7bit;
    dev->is_initialized = true;

    status = bq25896_verify_device_id(dev);
    if (BQ25896_FAILED(status))
    {
        dev->is_initialized = false;
        return status;
    }

    if (config->reset_registers_on_init)
    {
        status = bq25896_reset(dev);
        if (BQ25896_FAILED(status))
        {
            dev->is_initialized = false;
            return status;
        }
    }

    status = bq25896_apply_init_config(dev, config);
    if (BQ25896_FAILED(status))
    {
        dev->is_initialized = false;
        return status;
    }

    return BQ25896_OK;
}

bq25896_err_t bq25896_reset(bq25896_t *dev)
{
    bq25896_err_t status = bq25896_validate_device(dev);

    if (BQ25896_FAILED(status))
    {
        return status;
    }

    status = bq25896_update_bits(dev,
                                 BQ25896_REG_14,
                                 BQ25896_REG14_REG_RST_MASK,
                                 (uint8_t)BQ25896_FIELD_PREP(BQ25896_REG14_REG_RST_MASK,
                                                             BQ25896_REG14_REG_RST_SHIFT,
                                                             1u));
    if (BQ25896_FAILED(status))
    {
        return status;
    }

    return bq25896_wait_for_bit_clear(dev,
                                      BQ25896_REG_14,
                                      BQ25896_REG14_REG_RST_MASK,
                                      BQ25896_RESET_POLL_COUNT,
                                      BQ25896_RESET_POLL_DELAY_MS);
}

bq25896_err_t bq25896_enable_charge(bq25896_t *dev)
{
    return bq25896_update_bits(dev,
                               BQ25896_REG_03,
                               BQ25896_REG03_CHG_CONFIG_MASK,
                               (uint8_t)BQ25896_FIELD_PREP(BQ25896_REG03_CHG_CONFIG_MASK,
                                                           BQ25896_REG03_CHG_CONFIG_SHIFT,
                                                           1u));
}

bq25896_err_t bq25896_disable_charge(bq25896_t *dev)
{
    return bq25896_update_bits(dev,
                               BQ25896_REG_03,
                               BQ25896_REG03_CHG_CONFIG_MASK,
                               (uint8_t)BQ25896_FIELD_PREP(BQ25896_REG03_CHG_CONFIG_MASK,
                                                           BQ25896_REG03_CHG_CONFIG_SHIFT,
                                                           0u));
}

bq25896_err_t bq25896_set_input_limit_ma(bq25896_t *dev, uint16_t input_limit_ma)
{
    bq25896_quantized_value_t quantized = bq25896_quantize_u16(input_limit_ma,
                                                               BQ25896_IINLIM_MIN_MA,
                                                               BQ25896_IINLIM_MAX_MA,
                                                               BQ25896_IINLIM_OFFSET_MA,
                                                               BQ25896_IINLIM_STEP_MA,
                                                               BQ25896_IINLIM_RAW_MAX);
    bq25896_err_t status = bq25896_update_bits(dev,
                                               BQ25896_REG_00,
                                               BQ25896_REG00_IINLIM_MASK,
                                               (uint8_t)BQ25896_FIELD_PREP(BQ25896_REG00_IINLIM_MASK,
                                                                           BQ25896_REG00_IINLIM_SHIFT,
                                                                           quantized.reg_value));

    if (BQ25896_FAILED(status))
    {
        return status;
    }

    return quantized.status;
}

bq25896_err_t bq25896_set_charge_current_ma(bq25896_t *dev, uint16_t charge_current_ma)
{
    bq25896_quantized_value_t quantized = bq25896_quantize_u16(charge_current_ma,
                                                               BQ25896_ICHG_MIN_MA,
                                                               BQ25896_ICHG_MAX_MA,
                                                               BQ25896_ICHG_OFFSET_MA,
                                                               BQ25896_ICHG_STEP_MA,
                                                               BQ25896_ICHG_RAW_MAX);
    bq25896_err_t status = bq25896_update_bits(dev,
                                               BQ25896_REG_04,
                                               BQ25896_REG04_ICHG_MASK,
                                               (uint8_t)BQ25896_FIELD_PREP(BQ25896_REG04_ICHG_MASK,
                                                                           BQ25896_REG04_ICHG_SHIFT,
                                                                           quantized.reg_value));

    if (BQ25896_FAILED(status))
    {
        return status;
    }

    return quantized.status;
}

bq25896_err_t bq25896_set_precharge_current_ma(bq25896_t *dev, uint16_t precharge_current_ma)
{
    bq25896_quantized_value_t quantized = bq25896_quantize_u16(precharge_current_ma,
                                                               BQ25896_IPRECHG_MIN_MA,
                                                               BQ25896_IPRECHG_MAX_MA,
                                                               BQ25896_IPRECHG_OFFSET_MA,
                                                               BQ25896_IPRECHG_STEP_MA,
                                                               BQ25896_IPRECHG_RAW_MAX);
    bq25896_err_t status = bq25896_update_bits(dev,
                                               BQ25896_REG_05,
                                               BQ25896_REG05_IPRECHG_MASK,
                                               (uint8_t)BQ25896_FIELD_PREP(BQ25896_REG05_IPRECHG_MASK,
                                                                           BQ25896_REG05_IPRECHG_SHIFT,
                                                                           quantized.reg_value));

    if (BQ25896_FAILED(status))
    {
        return status;
    }

    return quantized.status;
}

bq25896_err_t bq25896_set_termination_current_ma(bq25896_t *dev, uint16_t termination_current_ma)
{
    bq25896_quantized_value_t quantized = bq25896_quantize_u16(termination_current_ma,
                                                               BQ25896_ITERM_MIN_MA,
                                                               BQ25896_ITERM_MAX_MA,
                                                               BQ25896_ITERM_OFFSET_MA,
                                                               BQ25896_ITERM_STEP_MA,
                                                               BQ25896_ITERM_RAW_MAX);
    bq25896_err_t status = bq25896_update_bits(dev,
                                               BQ25896_REG_05,
                                               BQ25896_REG05_ITERM_MASK,
                                               (uint8_t)BQ25896_FIELD_PREP(BQ25896_REG05_ITERM_MASK,
                                                                           BQ25896_REG05_ITERM_SHIFT,
                                                                           quantized.reg_value));

    if (BQ25896_FAILED(status))
    {
        return status;
    }

    return quantized.status;
}

bq25896_err_t bq25896_set_charge_voltage_mv(bq25896_t *dev, uint16_t charge_voltage_mv)
{
    bq25896_quantized_value_t quantized = bq25896_quantize_u16(charge_voltage_mv,
                                                               BQ25896_VREG_MIN_MV,
                                                               BQ25896_VREG_MAX_MV,
                                                               BQ25896_VREG_OFFSET_MV,
                                                               BQ25896_VREG_STEP_MV,
                                                               BQ25896_VREG_RAW_MAX);
    bq25896_err_t status = bq25896_update_bits(dev,
                                               BQ25896_REG_06,
                                               BQ25896_REG06_VREG_MASK,
                                               (uint8_t)BQ25896_FIELD_PREP(BQ25896_REG06_VREG_MASK,
                                                                           BQ25896_REG06_VREG_SHIFT,
                                                                           quantized.reg_value));

    if (BQ25896_FAILED(status))
    {
        return status;
    }

    return quantized.status;
}

bq25896_err_t bq25896_set_system_min_voltage_mv(bq25896_t *dev, uint16_t sys_min_voltage_mv)
{
    bq25896_quantized_value_t quantized = bq25896_quantize_u16(sys_min_voltage_mv,
                                                               BQ25896_SYS_MIN_MIN_MV,
                                                               BQ25896_SYS_MIN_MAX_MV,
                                                               BQ25896_SYS_MIN_OFFSET_MV,
                                                               BQ25896_SYS_MIN_STEP_MV,
                                                               BQ25896_SYS_MIN_RAW_MAX);
    bq25896_err_t status = bq25896_update_bits(dev,
                                               BQ25896_REG_03,
                                               BQ25896_REG03_SYS_MIN_MASK,
                                               (uint8_t)BQ25896_FIELD_PREP(BQ25896_REG03_SYS_MIN_MASK,
                                                                           BQ25896_REG03_SYS_MIN_SHIFT,
                                                                           quantized.reg_value));

    if (BQ25896_FAILED(status))
    {
        return status;
    }

    return quantized.status;
}

bq25896_err_t bq25896_enable_otg(bq25896_t *dev)
{
    return bq25896_update_bits(dev,
                               BQ25896_REG_03,
                               BQ25896_REG03_OTG_CONFIG_MASK,
                               (uint8_t)BQ25896_FIELD_PREP(BQ25896_REG03_OTG_CONFIG_MASK,
                                                           BQ25896_REG03_OTG_CONFIG_SHIFT,
                                                           1u));
}

bq25896_err_t bq25896_disable_otg(bq25896_t *dev)
{
    return bq25896_update_bits(dev,
                               BQ25896_REG_03,
                               BQ25896_REG03_OTG_CONFIG_MASK,
                               (uint8_t)BQ25896_FIELD_PREP(BQ25896_REG03_OTG_CONFIG_MASK,
                                                           BQ25896_REG03_OTG_CONFIG_SHIFT,
                                                           0u));
}

bq25896_err_t bq25896_enable_battery_power_path(bq25896_t *dev)
{
    bq25896_err_t status = bq25896_validate_device(dev);

    if (BQ25896_FAILED(status))
    {
        return status;
    }

    status = bq25896_update_bits(dev,
                                 BQ25896_REG_09,
                                 BQ25896_REG09_BATFET_DIS_MASK,
                                 (uint8_t)BQ25896_FIELD_PREP(BQ25896_REG09_BATFET_DIS_MASK,
                                                             BQ25896_REG09_BATFET_DIS_SHIFT,
                                                             0u));
    if (BQ25896_FAILED(status))
    {
        return status;
    }

    return bq25896_update_bits(dev,
                               BQ25896_REG_00,
                               BQ25896_REG00_EN_HIZ_MASK,
                               (uint8_t)BQ25896_FIELD_PREP(BQ25896_REG00_EN_HIZ_MASK,
                                                           BQ25896_REG00_EN_HIZ_SHIFT,
                                                           0u));
}

bq25896_err_t bq25896_set_otg_voltage_mv(bq25896_t *dev, uint16_t otg_voltage_mv)
{
    bq25896_quantized_value_t quantized = bq25896_quantize_u16(otg_voltage_mv,
                                                               BQ25896_BOOSTV_MIN_MV,
                                                               BQ25896_BOOSTV_MAX_MV,
                                                               BQ25896_BOOSTV_OFFSET_MV,
                                                               BQ25896_BOOSTV_STEP_MV,
                                                               BQ25896_BOOSTV_RAW_MAX);
    bq25896_err_t status = bq25896_update_bits(dev,
                                               BQ25896_REG_0A,
                                               BQ25896_REG0A_BOOSTV_MASK,
                                               (uint8_t)BQ25896_FIELD_PREP(BQ25896_REG0A_BOOSTV_MASK,
                                                                           BQ25896_REG0A_BOOSTV_SHIFT,
                                                                           quantized.reg_value));

    if (BQ25896_FAILED(status))
    {
        return status;
    }

    return quantized.status;
}

bq25896_err_t bq25896_kick_watchdog(bq25896_t *dev)
{
    return bq25896_update_bits(dev,
                               BQ25896_REG_03,
                               BQ25896_REG03_WD_RST_MASK,
                               (uint8_t)BQ25896_FIELD_PREP(BQ25896_REG03_WD_RST_MASK,
                                                           BQ25896_REG03_WD_RST_SHIFT,
                                                           1u));
}

bq25896_err_t bq25896_shutdown(bq25896_t *dev)
{
    bq25896_err_t status = bq25896_validate_device(dev);

    if (BQ25896_FAILED(status))
    {
        return status;
    }

    status = bq25896_disable_otg(dev);
    if (BQ25896_FAILED(status))
    {
        return status;
    }

    status = bq25896_disable_charge(dev);
    if (BQ25896_FAILED(status))
    {
        return status;
    }

    return bq25896_update_bits(dev,
                               BQ25896_REG_09,
                               BQ25896_REG09_BATFET_DIS_MASK,
                               (uint8_t)BQ25896_FIELD_PREP(BQ25896_REG09_BATFET_DIS_MASK,
                                                           BQ25896_REG09_BATFET_DIS_SHIFT,
                                                           1u));
}

bq25896_err_t bq25896_read_status(const bq25896_t *dev, bq25896_status_t *status)
{
    bq25896_err_t rc = bq25896_validate_device(dev);

    if (BQ25896_FAILED(rc))
    {
        return rc;
    }

    if (status == NULL)
    {
        return BQ25896_ERR_NULL;
    }

    memset(status, 0, sizeof(*status));

    rc = bq25896_read_reg(dev, BQ25896_REG_0B, &status->raw_reg0b);
    if (BQ25896_FAILED(rc))
    {
        return rc;
    }

    rc = bq25896_read_reg(dev, BQ25896_REG_11, &status->raw_reg11);
    if (BQ25896_FAILED(rc))
    {
        return rc;
    }

    rc = bq25896_read_reg(dev, BQ25896_REG_13, &status->raw_reg13);
    if (BQ25896_FAILED(rc))
    {
        return rc;
    }

    rc = bq25896_read_reg(dev, BQ25896_REG_14, &status->raw_reg14);
    if (BQ25896_FAILED(rc))
    {
        return rc;
    }

    status->vbus_status = (bq25896_vbus_status_t)BQ25896_FIELD_GET(BQ25896_REG0B_VBUS_STAT_MASK,
                                                                   BQ25896_REG0B_VBUS_STAT_SHIFT,
                                                                   status->raw_reg0b);
    status->charge_status = (bq25896_charge_status_t)BQ25896_FIELD_GET(BQ25896_REG0B_CHRG_STAT_MASK,
                                                                       BQ25896_REG0B_CHRG_STAT_SHIFT,
                                                                       status->raw_reg0b);
    status->power_good = (status->raw_reg0b & BQ25896_REG0B_PG_STAT_MASK) != 0u;
    status->vbus_good = (status->raw_reg11 & BQ25896_REG11_VBUS_GD_MASK) != 0u;
    status->vsys_regulated = (status->raw_reg0b & BQ25896_REG0B_VSYS_STAT_MASK) != 0u;
    status->vindpm_active = (status->raw_reg13 & BQ25896_REG13_VDPM_STAT_MASK) != 0u;
    status->iindpm_active = (status->raw_reg13 & BQ25896_REG13_IDPM_STAT_MASK) != 0u;
    status->ico_optimized = (status->raw_reg14 & BQ25896_REG14_ICO_OPTIMIZED_MASK) != 0u;
    status->ts_profile_jeita = (status->raw_reg14 & BQ25896_REG14_TS_PROFILE_MASK) != 0u;
    status->input_limit_ma = bq25896_decode_u16(status->raw_reg13,
                                                BQ25896_REG13_IDPM_LIM_MASK,
                                                BQ25896_REG13_IDPM_LIM_SHIFT,
                                                BQ25896_ADC_IDPM_LIM_OFFSET_MA,
                                                BQ25896_ADC_IDPM_LIM_STEP_MA);
    status->part_number = (uint8_t)BQ25896_FIELD_GET(BQ25896_REG14_PN_MASK,
                                                     BQ25896_REG14_PN_SHIFT,
                                                     status->raw_reg14);
    status->device_revision = (uint8_t)BQ25896_FIELD_GET(BQ25896_REG14_DEV_REV_MASK,
                                                         BQ25896_REG14_DEV_REV_SHIFT,
                                                         status->raw_reg14);

    return BQ25896_OK;
}

bq25896_err_t bq25896_read_fault(const bq25896_t *dev, bq25896_fault_t *fault)
{
    bq25896_err_t rc = bq25896_validate_device(dev);

    if (BQ25896_FAILED(rc))
    {
        return rc;
    }

    if (fault == NULL)
    {
        return BQ25896_ERR_NULL;
    }

    memset(fault, 0, sizeof(*fault));

    rc = bq25896_read_reg(dev, BQ25896_REG_0C, &fault->raw_reg0c);
    if (BQ25896_FAILED(rc))
    {
        return rc;
    }

    fault->watchdog_fault = (fault->raw_reg0c & BQ25896_REG0C_WATCHDOG_FAULT_MASK) != 0u;
    fault->boost_fault = (fault->raw_reg0c & BQ25896_REG0C_BOOST_FAULT_MASK) != 0u;
    fault->battery_fault = (fault->raw_reg0c & BQ25896_REG0C_BAT_FAULT_MASK) != 0u;
    fault->charge_fault = (bq25896_charge_fault_t)BQ25896_FIELD_GET(BQ25896_REG0C_CHRG_FAULT_MASK,
                                                                    BQ25896_REG0C_CHRG_FAULT_SHIFT,
                                                                    fault->raw_reg0c);
    fault->ntc_fault = (bq25896_ntc_fault_t)BQ25896_FIELD_GET(BQ25896_REG0C_NTC_FAULT_MASK,
                                                              BQ25896_REG0C_NTC_FAULT_SHIFT,
                                                              fault->raw_reg0c);

    return BQ25896_OK;
}

bq25896_err_t bq25896_read_adc(const bq25896_t *dev, bq25896_adc_t *adc)
{
    uint8_t reg02 = 0u;
    uint8_t buffer[BQ25896_ADC_BLOCK_LEN] = {0u};
    bq25896_err_t rc = bq25896_validate_device(dev);

    if (BQ25896_FAILED(rc))
    {
        return rc;
    }

    if (adc == NULL)
    {
        return BQ25896_ERR_NULL;
    }

    memset(adc, 0, sizeof(*adc));

    rc = bq25896_read_reg(dev, BQ25896_REG_02, &reg02);
    if (BQ25896_FAILED(rc))
    {
        return rc;
    }

    if (BQ25896_FIELD_GET(BQ25896_REG02_CONV_RATE_MASK, BQ25896_REG02_CONV_RATE_SHIFT, reg02) ==
        BQ25896_ADC_MODE_ONE_SHOT)
    {
        rc = bq25896_update_bits(dev,
                                 BQ25896_REG_02,
                                 BQ25896_REG02_CONV_START_MASK,
                                 (uint8_t)BQ25896_FIELD_PREP(BQ25896_REG02_CONV_START_MASK,
                                                             BQ25896_REG02_CONV_START_SHIFT,
                                                             1u));
        if (BQ25896_FAILED(rc))
        {
            return rc;
        }

        rc = bq25896_wait_for_bit_clear(dev,
                                        BQ25896_REG_02,
                                        BQ25896_REG02_CONV_START_MASK,
                                        BQ25896_ADC_POLL_COUNT,
                                        BQ25896_ADC_POLL_DELAY_MS);
        if (BQ25896_FAILED(rc))
        {
            return rc;
        }
    }

    rc = bq25896_read_block(dev, BQ25896_REG_0E, buffer, BQ25896_ADC_BLOCK_LEN);
    if (BQ25896_FAILED(rc))
    {
        return rc;
    }

    adc->raw_reg0e = buffer[0];
    adc->raw_reg0f = buffer[1];
    adc->raw_reg10 = buffer[2];
    adc->raw_reg11 = buffer[3];
    adc->raw_reg12 = buffer[4];
    adc->raw_reg13 = buffer[5];

    adc->thermal_regulation_active = (adc->raw_reg0e & BQ25896_REG0E_THERM_STAT_MASK) != 0u;
    adc->vbus_good = (adc->raw_reg11 & BQ25896_REG11_VBUS_GD_MASK) != 0u;
    adc->vindpm_active = (adc->raw_reg13 & BQ25896_REG13_VDPM_STAT_MASK) != 0u;
    adc->iindpm_active = (adc->raw_reg13 & BQ25896_REG13_IDPM_STAT_MASK) != 0u;
    adc->battery_voltage_mv = bq25896_decode_u16(adc->raw_reg0e,
                                                 BQ25896_REG0E_BATV_MASK,
                                                 BQ25896_REG0E_BATV_SHIFT,
                                                 BQ25896_ADC_BATV_OFFSET_MV,
                                                 BQ25896_ADC_BATV_STEP_MV);
    adc->system_voltage_mv = bq25896_decode_u16(adc->raw_reg0f,
                                                BQ25896_REG0F_SYSV_MASK,
                                                BQ25896_REG0F_SYSV_SHIFT,
                                                BQ25896_ADC_SYSV_OFFSET_MV,
                                                BQ25896_ADC_SYSV_STEP_MV);
    adc->ts_percent_x1000 = bq25896_decode_u32(adc->raw_reg10,
                                               BQ25896_REG10_TSPCT_MASK,
                                               BQ25896_REG10_TSPCT_SHIFT,
                                               BQ25896_ADC_TSPCT_OFFSET_X1000,
                                               BQ25896_ADC_TSPCT_STEP_X1000);
    adc->vbus_voltage_mv = bq25896_decode_u16(adc->raw_reg11,
                                              BQ25896_REG11_VBUSV_MASK,
                                              BQ25896_REG11_VBUSV_SHIFT,
                                              BQ25896_ADC_VBUSV_OFFSET_MV,
                                              BQ25896_ADC_VBUSV_STEP_MV);
    adc->charge_current_ma = bq25896_decode_u16(adc->raw_reg12,
                                                BQ25896_REG12_ICHGR_MASK,
                                                BQ25896_REG12_ICHGR_SHIFT,
                                                BQ25896_ADC_ICHGR_OFFSET_MA,
                                                BQ25896_ADC_ICHGR_STEP_MA);
    adc->input_limit_ma = bq25896_decode_u16(adc->raw_reg13,
                                             BQ25896_REG13_IDPM_LIM_MASK,
                                             BQ25896_REG13_IDPM_LIM_SHIFT,
                                             BQ25896_ADC_IDPM_LIM_OFFSET_MA,
                                             BQ25896_ADC_IDPM_LIM_STEP_MA);

    return BQ25896_OK;
}

bq25896_err_t bq25896_read_charge_config(const bq25896_t *dev, bq25896_charge_config_t *cfg)
{
    bq25896_err_t rc = bq25896_validate_device(dev);

    if (BQ25896_FAILED(rc))
    {
        return rc;
    }

    if (cfg == NULL)
    {
        return BQ25896_ERR_NULL;
    }

    memset(cfg, 0, sizeof(*cfg));

    rc = bq25896_read_reg(dev, BQ25896_REG_00, &cfg->raw_reg00);
    if (BQ25896_FAILED(rc))
    {
        return rc;
    }

    rc = bq25896_read_reg(dev, BQ25896_REG_03, &cfg->raw_reg03);
    if (BQ25896_FAILED(rc))
    {
        return rc;
    }

    rc = bq25896_read_reg(dev, BQ25896_REG_04, &cfg->raw_reg04);
    if (BQ25896_FAILED(rc))
    {
        return rc;
    }

    rc = bq25896_read_reg(dev, BQ25896_REG_05, &cfg->raw_reg05);
    if (BQ25896_FAILED(rc))
    {
        return rc;
    }

    rc = bq25896_read_reg(dev, BQ25896_REG_06, &cfg->raw_reg06);
    if (BQ25896_FAILED(rc))
    {
        return rc;
    }

    rc = bq25896_read_reg(dev, BQ25896_REG_09, &cfg->raw_reg09);
    if (BQ25896_FAILED(rc))
    {
        return rc;
    }

    cfg->charge_enabled = (cfg->raw_reg03 & BQ25896_REG03_CHG_CONFIG_MASK) != 0u;
    cfg->otg_enabled = (cfg->raw_reg03 & BQ25896_REG03_OTG_CONFIG_MASK) != 0u;
    cfg->hiz_enabled = (cfg->raw_reg00 & BQ25896_REG00_EN_HIZ_MASK) != 0u;
    cfg->batfet_disabled = (cfg->raw_reg09 & BQ25896_REG09_BATFET_DIS_MASK) != 0u;
    cfg->sys_min_voltage_mv = bq25896_decode_u16(cfg->raw_reg03,
                                                 BQ25896_REG03_SYS_MIN_MASK,
                                                 BQ25896_REG03_SYS_MIN_SHIFT,
                                                 BQ25896_SYS_MIN_OFFSET_MV,
                                                 BQ25896_SYS_MIN_STEP_MV);
    cfg->charge_voltage_mv = bq25896_decode_u16(cfg->raw_reg06,
                                                BQ25896_REG06_VREG_MASK,
                                                BQ25896_REG06_VREG_SHIFT,
                                                BQ25896_VREG_OFFSET_MV,
                                                BQ25896_VREG_STEP_MV);
    cfg->charge_current_ma = bq25896_decode_u16(cfg->raw_reg04,
                                                BQ25896_REG04_ICHG_MASK,
                                                BQ25896_REG04_ICHG_SHIFT,
                                                BQ25896_ICHG_OFFSET_MA,
                                                BQ25896_ICHG_STEP_MA);
    cfg->precharge_current_ma = bq25896_decode_u16(cfg->raw_reg05,
                                                   BQ25896_REG05_IPRECHG_MASK,
                                                   BQ25896_REG05_IPRECHG_SHIFT,
                                                   BQ25896_IPRECHG_OFFSET_MA,
                                                   BQ25896_IPRECHG_STEP_MA);
    cfg->termination_current_ma = bq25896_decode_u16(cfg->raw_reg05,
                                                     BQ25896_REG05_ITERM_MASK,
                                                     BQ25896_REG05_ITERM_SHIFT,
                                                     BQ25896_ITERM_OFFSET_MA,
                                                     BQ25896_ITERM_STEP_MA);

    return BQ25896_OK;
}
