#ifndef BQ25896_H
#define BQ25896_H

#include <stdbool.h>
#include <stdint.h>

#include "bq25896_hal.h"
#include "bq25896_reg.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file bq25896.h
 * @brief Portable BQ25896 charger driver API.
 */

/**
 * @brief ADC conversion mode.
 */
typedef enum
{
    BQ25896_ADC_MODE_ONE_SHOT = 0u,
    BQ25896_ADC_MODE_CONTINUOUS = 1u
} bq25896_adc_mode_t;

/**
 * @brief Watchdog timer setting encoded in REG07[5:4].
 */
typedef enum
{
    BQ25896_WATCHDOG_DISABLED = 0u,
    BQ25896_WATCHDOG_40S = 1u,
    BQ25896_WATCHDOG_80S = 2u,
    BQ25896_WATCHDOG_160S = 3u
} bq25896_watchdog_t;

/**
 * @brief REG0B[7:5] VBUS source status code.
 */
typedef enum
{
    BQ25896_VBUS_STATUS_NO_INPUT = 0u,
    BQ25896_VBUS_STATUS_USB_SDP = 1u,
    BQ25896_VBUS_STATUS_ADAPTER = 2u,
    BQ25896_VBUS_STATUS_CODE_3 = 3u,
    BQ25896_VBUS_STATUS_CODE_4 = 4u,
    BQ25896_VBUS_STATUS_CODE_5 = 5u,
    BQ25896_VBUS_STATUS_CODE_6 = 6u,
    BQ25896_VBUS_STATUS_OTG = 7u
} bq25896_vbus_status_t;

/**
 * @brief REG0B[4:3] charging status.
 */
typedef enum
{
    BQ25896_CHARGE_STATUS_NOT_CHARGING = 0u,
    BQ25896_CHARGE_STATUS_PRECHARGE = 1u,
    BQ25896_CHARGE_STATUS_FAST_CHARGE = 2u,
    BQ25896_CHARGE_STATUS_TERMINATION_DONE = 3u
} bq25896_charge_status_t;

/**
 * @brief REG0C[5:4] charge fault status.
 */
typedef enum
{
    BQ25896_CHARGE_FAULT_NORMAL = 0u,
    BQ25896_CHARGE_FAULT_INPUT = 1u,
    BQ25896_CHARGE_FAULT_THERMAL_SHUTDOWN = 2u,
    BQ25896_CHARGE_FAULT_SAFETY_TIMER = 3u
} bq25896_charge_fault_t;

/**
 * @brief REG0C[2:0] NTC fault status.
 */
typedef enum
{
    BQ25896_NTC_FAULT_NORMAL = 0u,
    BQ25896_NTC_FAULT_CODE_1 = 1u,
    BQ25896_NTC_FAULT_WARM = 2u,
    BQ25896_NTC_FAULT_COOL = 3u,
    BQ25896_NTC_FAULT_CODE_4 = 4u,
    BQ25896_NTC_FAULT_COLD = 5u,
    BQ25896_NTC_FAULT_HOT = 6u,
    BQ25896_NTC_FAULT_CODE_7 = 7u
} bq25896_ntc_fault_t;

/**
 * @brief Driver instance handle.
 */
typedef struct
{
    bq25896_hal_t hal;      /**< User-supplied HAL callbacks. */
    uint8_t i2c_addr_7bit;  /**< BQ25896 7-bit I2C address. */
    bool is_initialized;    /**< Set after a successful `bq25896_init()`. */
} bq25896_t;

/**
 * @brief Initialization options.
 */
typedef struct
{
    bq25896_hal_t hal;               /**< User HAL callbacks and context. */
    uint8_t i2c_addr_7bit;           /**< Target 7-bit I2C address. */
    bool reset_registers_on_init;    /**< Reset the IC before applying options. */
    bool exit_hiz_on_init;           /**< Clear EN_HIZ to allow normal input path. */
    bool enable_ilim_pin;            /**< Mirror config into REG00.EN_ILIM. */
    bool enable_ico;                 /**< Mirror config into REG02.ICO_EN. */
    bq25896_adc_mode_t adc_mode;     /**< ADC mode written to REG02.CONV_RATE. */
    bq25896_watchdog_t watchdog;     /**< Watchdog value written to REG07. */
} bq25896_config_t;

/**
 * @brief Decoded charger status.
 */
typedef struct
{
    uint8_t raw_reg0b;                     /**< Raw REG0B value. */
    uint8_t raw_reg11;                     /**< Raw REG11 value. */
    uint8_t raw_reg13;                     /**< Raw REG13 value. */
    uint8_t raw_reg14;                     /**< Raw REG14 value. */
    bq25896_vbus_status_t vbus_status;     /**< Decoded VBUS source type. */
    bq25896_charge_status_t charge_status; /**< Decoded charge state. */
    bool power_good;                       /**< REG0B.PG_STAT. */
    bool vbus_good;                        /**< REG11.VBUS_GD. */
    bool vsys_regulated;                   /**< REG0B.VSYS_STAT. */
    bool vindpm_active;                    /**< REG13.VDPM_STAT. */
    bool iindpm_active;                    /**< REG13.IDPM_STAT. */
    bool ico_optimized;                    /**< REG14.ICO_OPTIMIZED. */
    bool ts_profile_jeita;                 /**< REG14.TS_PROFILE. */
    uint16_t input_limit_ma;               /**< REG13.IDPM_LIM decoded in mA. */
    uint8_t part_number;                   /**< REG14.PN raw value. */
    uint8_t device_revision;               /**< REG14.DEV_REV raw value. */
} bq25896_status_t;

/**
 * @brief Decoded fault status.
 */
typedef struct
{
    uint8_t raw_reg0c;                   /**< Raw REG0C value. */
    bool watchdog_fault;                 /**< REG0C.WATCHDOG_FAULT. */
    bool boost_fault;                    /**< REG0C.BOOST_FAULT. */
    bool battery_fault;                  /**< REG0C.BAT_FAULT. */
    bq25896_charge_fault_t charge_fault; /**< REG0C.CHRG_FAULT. */
    bq25896_ntc_fault_t ntc_fault;       /**< REG0C.NTC_FAULT. */
} bq25896_fault_t;

/**
 * @brief Decoded ADC readings.
 */
typedef struct
{
    uint8_t raw_reg0e;              /**< Raw REG0E value. */
    uint8_t raw_reg0f;              /**< Raw REG0F value. */
    uint8_t raw_reg10;              /**< Raw REG10 value. */
    uint8_t raw_reg11;              /**< Raw REG11 value. */
    uint8_t raw_reg12;              /**< Raw REG12 value. */
    uint8_t raw_reg13;              /**< Raw REG13 value. */
    bool thermal_regulation_active; /**< REG0E.THERM_STAT. */
    bool vbus_good;                 /**< REG11.VBUS_GD. */
    bool vindpm_active;             /**< REG13.VDPM_STAT. */
    bool iindpm_active;             /**< REG13.IDPM_STAT. */
    uint16_t battery_voltage_mv;    /**< VBAT ADC in mV. */
    uint16_t system_voltage_mv;     /**< VSYS ADC in mV. */
    uint32_t ts_percent_x1000;      /**< TS percentage in 0.001%% units. */
    uint16_t vbus_voltage_mv;       /**< VBUS ADC in mV. */
    uint16_t charge_current_ma;     /**< Charge current ADC in mA. */
    uint16_t input_limit_ma;        /**< IDPM limit in mA. */
} bq25896_adc_t;

/**
 * @brief Decoded programmable charge configuration.
 */
typedef struct
{
    uint8_t raw_reg00;             /**< Raw REG00 value. */
    uint8_t raw_reg03;             /**< Raw REG03 value. */
    uint8_t raw_reg04;             /**< Raw REG04 value. */
    uint8_t raw_reg05;             /**< Raw REG05 value. */
    uint8_t raw_reg06;             /**< Raw REG06 value. */
    uint8_t raw_reg09;             /**< Raw REG09 value. */
    bool charge_enabled;           /**< REG03.CHG_CONFIG. */
    bool otg_enabled;              /**< REG03.OTG_CONFIG. */
    bool hiz_enabled;              /**< REG00.EN_HIZ. */
    bool batfet_disabled;          /**< REG09.BATFET_DIS. */
    uint16_t sys_min_voltage_mv;   /**< REG03.SYS_MIN decoded in mV. */
    uint16_t charge_voltage_mv;    /**< REG06.VREG decoded in mV. */
    uint16_t charge_current_ma;    /**< REG04.ICHG decoded in mA. */
    uint16_t precharge_current_ma; /**< REG05.IPRECHG decoded in mA. */
    uint16_t termination_current_ma; /**< REG05.ITERM decoded in mA. */
} bq25896_charge_config_t;

/**
 * @brief Fill a config structure with safe defaults.
 *
 * @param config Output configuration pointer.
 *
 * @return `BQ25896_OK` on success.
 */
bq25896_err_t bq25896_get_default_config(bq25896_config_t *config);

/**
 * @brief Initialize a BQ25896 instance.
 *
 * @param dev Driver instance.
 * @param config Initialization options including HAL callbacks and I2C address.
 *
 * @return `BQ25896_OK` on success, otherwise a negative error code.
 */
bq25896_err_t bq25896_init(bq25896_t *dev, const bq25896_config_t *config);

/**
 * @brief Reset the charger through REG14.REG_RST.
 *
 * @param dev Driver instance.
 *
 * @return `BQ25896_OK` on success, otherwise a negative error code.
 */
bq25896_err_t bq25896_reset(bq25896_t *dev);

/**
 * @brief Enable charging.
 *
 * @param dev Driver instance.
 *
 * @return `BQ25896_OK` on success, otherwise a negative error code.
 */
bq25896_err_t bq25896_enable_charge(bq25896_t *dev);

/**
 * @brief Disable charging.
 *
 * @param dev Driver instance.
 *
 * @return `BQ25896_OK` on success, otherwise a negative error code.
 */
bq25896_err_t bq25896_disable_charge(bq25896_t *dev);

/**
 * @brief Program REG00.IINLIM in milliamps.
 *
 * @param dev Driver instance.
 * @param input_limit_ma Requested input current limit in mA.
 *
 * @return `BQ25896_OK`, `BQ25896_WARN_CLAMPED`, or a negative error code.
 */
bq25896_err_t bq25896_set_input_limit_ma(bq25896_t *dev, uint16_t input_limit_ma);

/**
 * @brief Program REG04.ICHG in milliamps.
 *
 * @param dev Driver instance.
 * @param charge_current_ma Requested fast-charge current in mA.
 *
 * @return `BQ25896_OK`, `BQ25896_WARN_CLAMPED`, or a negative error code.
 */
bq25896_err_t bq25896_set_charge_current_ma(bq25896_t *dev, uint16_t charge_current_ma);

/**
 * @brief Program REG05.IPRECHG in milliamps.
 *
 * @param dev Driver instance.
 * @param precharge_current_ma Requested precharge current in mA.
 *
 * @return `BQ25896_OK`, `BQ25896_WARN_CLAMPED`, or a negative error code.
 */
bq25896_err_t bq25896_set_precharge_current_ma(bq25896_t *dev, uint16_t precharge_current_ma);

/**
 * @brief Program REG05.ITERM in milliamps.
 *
 * @param dev Driver instance.
 * @param termination_current_ma Requested termination current in mA.
 *
 * @return `BQ25896_OK`, `BQ25896_WARN_CLAMPED`, or a negative error code.
 */
bq25896_err_t bq25896_set_termination_current_ma(bq25896_t *dev, uint16_t termination_current_ma);

/**
 * @brief Program REG06.VREG in millivolts.
 *
 * @param dev Driver instance.
 * @param charge_voltage_mv Requested regulation voltage in mV.
 *
 * @return `BQ25896_OK`, `BQ25896_WARN_CLAMPED`, or a negative error code.
 */
bq25896_err_t bq25896_set_charge_voltage_mv(bq25896_t *dev, uint16_t charge_voltage_mv);

/**
 * @brief Program REG03.SYS_MIN in millivolts.
 *
 * @param dev Driver instance.
 * @param sys_min_voltage_mv Requested minimum system voltage in mV.
 *
 * @return `BQ25896_OK`, `BQ25896_WARN_CLAMPED`, or a negative error code.
 */
bq25896_err_t bq25896_set_system_min_voltage_mv(bq25896_t *dev, uint16_t sys_min_voltage_mv);

/**
 * @brief Enable OTG boost mode.
 *
 * @param dev Driver instance.
 *
 * @return `BQ25896_OK` on success, otherwise a negative error code.
 */
bq25896_err_t bq25896_enable_otg(bq25896_t *dev);

/**
 * @brief Disable OTG boost mode.
 *
 * @param dev Driver instance.
 *
 * @return `BQ25896_OK` on success, otherwise a negative error code.
 */
bq25896_err_t bq25896_disable_otg(bq25896_t *dev);

/**
 * @brief Restore the normal battery power path.
 *
 * This helper clears BATFET disable and HIZ mode without changing OTG state.
 *
 * @param dev Driver instance.
 *
 * @return `BQ25896_OK` on success, otherwise a negative error code.
 */
bq25896_err_t bq25896_enable_battery_power_path(bq25896_t *dev);

/**
 * @brief Program REG0A.BOOSTV in millivolts.
 *
 * @param dev Driver instance.
 * @param otg_voltage_mv Requested OTG output voltage in mV.
 *
 * @return `BQ25896_OK`, `BQ25896_WARN_CLAMPED`, or a negative error code.
 */
bq25896_err_t bq25896_set_otg_voltage_mv(bq25896_t *dev, uint16_t otg_voltage_mv);

/**
 * @brief Reset the watchdog timer through REG03.WD_RST.
 *
 * @param dev Driver instance.
 *
 * @return `BQ25896_OK` on success, otherwise a negative error code.
 */
bq25896_err_t bq25896_kick_watchdog(bq25896_t *dev);

/**
 * @brief Enter BATFET shutdown mode.
 *
 * This helper disables charge and OTG first, then asserts REG09.BATFET_DIS.
 * If the system is running only from the battery, power may be removed
 * immediately after this call succeeds.
 *
 * @param dev Driver instance.
 *
 * @return `BQ25896_OK` on success, otherwise a negative error code.
 */
bq25896_err_t bq25896_shutdown(bq25896_t *dev);

/**
 * @brief Read and decode charger status.
 *
 * @param dev Driver instance.
 * @param status Output structure.
 *
 * @return `BQ25896_OK` on success, otherwise a negative error code.
 */
bq25896_err_t bq25896_read_status(const bq25896_t *dev, bq25896_status_t *status);

/**
 * @brief Read and decode the fault register.
 *
 * @param dev Driver instance.
 * @param fault Output structure.
 *
 * @return `BQ25896_OK` on success, otherwise a negative error code.
 */
bq25896_err_t bq25896_read_fault(const bq25896_t *dev, bq25896_fault_t *fault);

/**
 * @brief Read and decode ADC registers.
 *
 * @param dev Driver instance.
 * @param adc Output structure.
 *
 * @return `BQ25896_OK` on success, otherwise a negative error code.
 */
bq25896_err_t bq25896_read_adc(const bq25896_t *dev, bq25896_adc_t *adc);

/**
 * @brief Read and decode the programmable charge configuration.
 *
 * @param dev Driver instance.
 * @param cfg Output structure.
 *
 * @return `BQ25896_OK` on success, otherwise a negative error code.
 */
bq25896_err_t bq25896_read_charge_config(const bq25896_t *dev, bq25896_charge_config_t *cfg);

#ifdef __cplusplus
}
#endif

#endif /* BQ25896_H */
