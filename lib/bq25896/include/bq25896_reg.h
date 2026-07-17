#ifndef BQ25896_REG_H
#define BQ25896_REG_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file bq25896_reg.h
 * @brief BQ25896 register addresses, bit fields, and conversion constants.
 */

#define BQ25896_REG_FIRST                         0x00u
#define BQ25896_REG_LAST                          0x14u
#define BQ25896_REG_COUNT                         ((BQ25896_REG_LAST - BQ25896_REG_FIRST) + 1u)

#define BQ25896_I2C_ADDR_7BIT_MIN                0x00u
#define BQ25896_I2C_ADDR_7BIT_MAX                0x7Fu
#define BQ25896_I2C_ADDR_7BIT_DEFAULT            0x6Bu

#define BQ25896_FIELD_PREP(mask, shift, value)   ((((unsigned int)(value)) << (shift)) & (mask))
#define BQ25896_FIELD_GET(mask, shift, reg)      ((((unsigned int)(reg)) & (mask)) >> (shift))

/* Register address map: 0x00 ~ 0x14 */
#define BQ25896_REG_00                            0x00u
#define BQ25896_REG_01                            0x01u
#define BQ25896_REG_02                            0x02u
#define BQ25896_REG_03                            0x03u
#define BQ25896_REG_04                            0x04u
#define BQ25896_REG_05                            0x05u
#define BQ25896_REG_06                            0x06u
#define BQ25896_REG_07                            0x07u
#define BQ25896_REG_08                            0x08u
#define BQ25896_REG_09                            0x09u
#define BQ25896_REG_0A                            0x0Au
#define BQ25896_REG_0B                            0x0Bu
#define BQ25896_REG_0C                            0x0Cu
#define BQ25896_REG_0D                            0x0Du
#define BQ25896_REG_0E                            0x0Eu
#define BQ25896_REG_0F                            0x0Fu
#define BQ25896_REG_10                            0x10u
#define BQ25896_REG_11                            0x11u
#define BQ25896_REG_12                            0x12u
#define BQ25896_REG_13                            0x13u
#define BQ25896_REG_14                            0x14u

/* REG00 Input Source Control */
#define BQ25896_REG00_EN_HIZ_SHIFT               7u
#define BQ25896_REG00_EN_HIZ_MASK                (0x01u << BQ25896_REG00_EN_HIZ_SHIFT)
#define BQ25896_REG00_EN_ILIM_SHIFT              6u
#define BQ25896_REG00_EN_ILIM_MASK               (0x01u << BQ25896_REG00_EN_ILIM_SHIFT)
#define BQ25896_REG00_IINLIM_SHIFT               0u
#define BQ25896_REG00_IINLIM_MASK                (0x3Fu << BQ25896_REG00_IINLIM_SHIFT)

/* REG01 Power-On Configuration */
#define BQ25896_REG01_BHOT_SHIFT                 6u
#define BQ25896_REG01_BHOT_MASK                  (0x03u << BQ25896_REG01_BHOT_SHIFT)
#define BQ25896_REG01_BCOLD_SHIFT                5u
#define BQ25896_REG01_BCOLD_MASK                 (0x01u << BQ25896_REG01_BCOLD_SHIFT)
#define BQ25896_REG01_VINDPM_OS_SHIFT            0u
#define BQ25896_REG01_VINDPM_OS_MASK             (0x1Fu << BQ25896_REG01_VINDPM_OS_SHIFT)

/* REG02 Charge Current Control */
#define BQ25896_REG02_CONV_START_SHIFT           7u
#define BQ25896_REG02_CONV_START_MASK            (0x01u << BQ25896_REG02_CONV_START_SHIFT)
#define BQ25896_REG02_CONV_RATE_SHIFT            6u
#define BQ25896_REG02_CONV_RATE_MASK             (0x01u << BQ25896_REG02_CONV_RATE_SHIFT)
#define BQ25896_REG02_BOOST_FREQ_SHIFT           5u
#define BQ25896_REG02_BOOST_FREQ_MASK            (0x01u << BQ25896_REG02_BOOST_FREQ_SHIFT)
#define BQ25896_REG02_ICO_EN_SHIFT               4u
#define BQ25896_REG02_ICO_EN_MASK                (0x01u << BQ25896_REG02_ICO_EN_SHIFT)
#define BQ25896_REG02_FORCE_DPDM_SHIFT           1u
#define BQ25896_REG02_FORCE_DPDM_MASK            (0x01u << BQ25896_REG02_FORCE_DPDM_SHIFT)
#define BQ25896_REG02_AUTO_DPDM_EN_SHIFT         0u
#define BQ25896_REG02_AUTO_DPDM_EN_MASK          (0x01u << BQ25896_REG02_AUTO_DPDM_EN_SHIFT)

/* REG03 Charge Voltage Control */
#define BQ25896_REG03_BAT_LOADEN_SHIFT           7u
#define BQ25896_REG03_BAT_LOADEN_MASK            (0x01u << BQ25896_REG03_BAT_LOADEN_SHIFT)
#define BQ25896_REG03_WD_RST_SHIFT               6u
#define BQ25896_REG03_WD_RST_MASK                (0x01u << BQ25896_REG03_WD_RST_SHIFT)
#define BQ25896_REG03_OTG_CONFIG_SHIFT           5u
#define BQ25896_REG03_OTG_CONFIG_MASK            (0x01u << BQ25896_REG03_OTG_CONFIG_SHIFT)
#define BQ25896_REG03_CHG_CONFIG_SHIFT           4u
#define BQ25896_REG03_CHG_CONFIG_MASK            (0x01u << BQ25896_REG03_CHG_CONFIG_SHIFT)
#define BQ25896_REG03_SYS_MIN_SHIFT              1u
#define BQ25896_REG03_SYS_MIN_MASK               (0x07u << BQ25896_REG03_SYS_MIN_SHIFT)
#define BQ25896_REG03_MIN_VBAT_SEL_SHIFT         0u
#define BQ25896_REG03_MIN_VBAT_SEL_MASK          (0x01u << BQ25896_REG03_MIN_VBAT_SEL_SHIFT)

/* REG04 Charge Current Control */
#define BQ25896_REG04_EN_PUMPX_SHIFT             7u
#define BQ25896_REG04_EN_PUMPX_MASK              (0x01u << BQ25896_REG04_EN_PUMPX_SHIFT)
#define BQ25896_REG04_ICHG_SHIFT                 0u
#define BQ25896_REG04_ICHG_MASK                  (0x7Fu << BQ25896_REG04_ICHG_SHIFT)

/* REG05 Precharge / Termination Current Control */
#define BQ25896_REG05_IPRECHG_SHIFT              4u
#define BQ25896_REG05_IPRECHG_MASK               (0x0Fu << BQ25896_REG05_IPRECHG_SHIFT)
#define BQ25896_REG05_ITERM_SHIFT                0u
#define BQ25896_REG05_ITERM_MASK                 (0x0Fu << BQ25896_REG05_ITERM_SHIFT)

/* REG06 Charge Voltage Control */
#define BQ25896_REG06_VREG_SHIFT                 2u
#define BQ25896_REG06_VREG_MASK                  (0x3Fu << BQ25896_REG06_VREG_SHIFT)
#define BQ25896_REG06_BATLOWV_SHIFT              1u
#define BQ25896_REG06_BATLOWV_MASK               (0x01u << BQ25896_REG06_BATLOWV_SHIFT)
#define BQ25896_REG06_VRECHG_SHIFT               0u
#define BQ25896_REG06_VRECHG_MASK                (0x01u << BQ25896_REG06_VRECHG_SHIFT)

/* REG07 Misc Operation Control */
#define BQ25896_REG07_EN_TERM_SHIFT              7u
#define BQ25896_REG07_EN_TERM_MASK               (0x01u << BQ25896_REG07_EN_TERM_SHIFT)
#define BQ25896_REG07_STAT_DIS_SHIFT             6u
#define BQ25896_REG07_STAT_DIS_MASK              (0x01u << BQ25896_REG07_STAT_DIS_SHIFT)
#define BQ25896_REG07_WATCHDOG_SHIFT             4u
#define BQ25896_REG07_WATCHDOG_MASK              (0x03u << BQ25896_REG07_WATCHDOG_SHIFT)
#define BQ25896_REG07_EN_TIMER_SHIFT             3u
#define BQ25896_REG07_EN_TIMER_MASK              (0x01u << BQ25896_REG07_EN_TIMER_SHIFT)
#define BQ25896_REG07_CHG_TIMER_SHIFT            1u
#define BQ25896_REG07_CHG_TIMER_MASK             (0x03u << BQ25896_REG07_CHG_TIMER_SHIFT)
#define BQ25896_REG07_JEITA_ISET_SHIFT           0u
#define BQ25896_REG07_JEITA_ISET_MASK            (0x01u << BQ25896_REG07_JEITA_ISET_SHIFT)

/* REG08 Thermal Regulation Control */
#define BQ25896_REG08_BAT_COMP_SHIFT             5u
#define BQ25896_REG08_BAT_COMP_MASK              (0x07u << BQ25896_REG08_BAT_COMP_SHIFT)
#define BQ25896_REG08_VCLAMP_SHIFT               2u
#define BQ25896_REG08_VCLAMP_MASK                (0x07u << BQ25896_REG08_VCLAMP_SHIFT)
#define BQ25896_REG08_TREG_SHIFT                 0u
#define BQ25896_REG08_TREG_MASK                  (0x03u << BQ25896_REG08_TREG_SHIFT)

/* REG09 Misc Operation Control */
#define BQ25896_REG09_FORCE_ICO_SHIFT            7u
#define BQ25896_REG09_FORCE_ICO_MASK             (0x01u << BQ25896_REG09_FORCE_ICO_SHIFT)
#define BQ25896_REG09_TMR2X_EN_SHIFT             6u
#define BQ25896_REG09_TMR2X_EN_MASK              (0x01u << BQ25896_REG09_TMR2X_EN_SHIFT)
#define BQ25896_REG09_BATFET_DIS_SHIFT           5u
#define BQ25896_REG09_BATFET_DIS_MASK            (0x01u << BQ25896_REG09_BATFET_DIS_SHIFT)
#define BQ25896_REG09_JEITA_VSET_SHIFT           4u
#define BQ25896_REG09_JEITA_VSET_MASK            (0x01u << BQ25896_REG09_JEITA_VSET_SHIFT)
#define BQ25896_REG09_BATFET_DLY_SHIFT           3u
#define BQ25896_REG09_BATFET_DLY_MASK            (0x01u << BQ25896_REG09_BATFET_DLY_SHIFT)
#define BQ25896_REG09_BATFET_RST_EN_SHIFT        2u
#define BQ25896_REG09_BATFET_RST_EN_MASK         (0x01u << BQ25896_REG09_BATFET_RST_EN_SHIFT)
#define BQ25896_REG09_PUMPX_UP_SHIFT             1u
#define BQ25896_REG09_PUMPX_UP_MASK              (0x01u << BQ25896_REG09_PUMPX_UP_SHIFT)
#define BQ25896_REG09_PUMPX_DN_SHIFT             0u
#define BQ25896_REG09_PUMPX_DN_MASK              (0x01u << BQ25896_REG09_PUMPX_DN_SHIFT)

/* REG0A Boost Voltage / Current Limit Control */
#define BQ25896_REG0A_BOOSTV_SHIFT               4u
#define BQ25896_REG0A_BOOSTV_MASK                (0x0Fu << BQ25896_REG0A_BOOSTV_SHIFT)
#define BQ25896_REG0A_PFM_OTG_DIS_SHIFT          3u
#define BQ25896_REG0A_PFM_OTG_DIS_MASK           (0x01u << BQ25896_REG0A_PFM_OTG_DIS_SHIFT)
#define BQ25896_REG0A_BOOST_LIM_SHIFT            0u
#define BQ25896_REG0A_BOOST_LIM_MASK             (0x07u << BQ25896_REG0A_BOOST_LIM_SHIFT)

/* REG0B System Status */
#define BQ25896_REG0B_VBUS_STAT_SHIFT            5u
#define BQ25896_REG0B_VBUS_STAT_MASK             (0x07u << BQ25896_REG0B_VBUS_STAT_SHIFT)
#define BQ25896_REG0B_CHRG_STAT_SHIFT            3u
#define BQ25896_REG0B_CHRG_STAT_MASK             (0x03u << BQ25896_REG0B_CHRG_STAT_SHIFT)
#define BQ25896_REG0B_PG_STAT_SHIFT              2u
#define BQ25896_REG0B_PG_STAT_MASK               (0x01u << BQ25896_REG0B_PG_STAT_SHIFT)
#define BQ25896_REG0B_VSYS_STAT_SHIFT            0u
#define BQ25896_REG0B_VSYS_STAT_MASK             (0x01u << BQ25896_REG0B_VSYS_STAT_SHIFT)

/* REG0C Fault */
#define BQ25896_REG0C_WATCHDOG_FAULT_SHIFT       7u
#define BQ25896_REG0C_WATCHDOG_FAULT_MASK        (0x01u << BQ25896_REG0C_WATCHDOG_FAULT_SHIFT)
#define BQ25896_REG0C_BOOST_FAULT_SHIFT          6u
#define BQ25896_REG0C_BOOST_FAULT_MASK           (0x01u << BQ25896_REG0C_BOOST_FAULT_SHIFT)
#define BQ25896_REG0C_CHRG_FAULT_SHIFT           4u
#define BQ25896_REG0C_CHRG_FAULT_MASK            (0x03u << BQ25896_REG0C_CHRG_FAULT_SHIFT)
#define BQ25896_REG0C_BAT_FAULT_SHIFT            3u
#define BQ25896_REG0C_BAT_FAULT_MASK             (0x01u << BQ25896_REG0C_BAT_FAULT_SHIFT)
#define BQ25896_REG0C_NTC_FAULT_SHIFT            0u
#define BQ25896_REG0C_NTC_FAULT_MASK             (0x07u << BQ25896_REG0C_NTC_FAULT_SHIFT)

/* REG0D Input Voltage Limit */
#define BQ25896_REG0D_FORCE_VINDPM_SHIFT         7u
#define BQ25896_REG0D_FORCE_VINDPM_MASK          (0x01u << BQ25896_REG0D_FORCE_VINDPM_SHIFT)
#define BQ25896_REG0D_VINDPM_SHIFT               0u
#define BQ25896_REG0D_VINDPM_MASK                (0x7Fu << BQ25896_REG0D_VINDPM_SHIFT)

/* REG0E Battery Voltage ADC */
#define BQ25896_REG0E_THERM_STAT_SHIFT           7u
#define BQ25896_REG0E_THERM_STAT_MASK            (0x01u << BQ25896_REG0E_THERM_STAT_SHIFT)
#define BQ25896_REG0E_BATV_SHIFT                 0u
#define BQ25896_REG0E_BATV_MASK                  (0x7Fu << BQ25896_REG0E_BATV_SHIFT)

/* REG0F System Voltage ADC */
#define BQ25896_REG0F_SYSV_SHIFT                 0u
#define BQ25896_REG0F_SYSV_MASK                  (0x7Fu << BQ25896_REG0F_SYSV_SHIFT)

/* REG10 TS ADC */
#define BQ25896_REG10_TSPCT_SHIFT                0u
#define BQ25896_REG10_TSPCT_MASK                 (0x7Fu << BQ25896_REG10_TSPCT_SHIFT)

/* REG11 VBUS Voltage ADC */
#define BQ25896_REG11_VBUS_GD_SHIFT              7u
#define BQ25896_REG11_VBUS_GD_MASK               (0x01u << BQ25896_REG11_VBUS_GD_SHIFT)
#define BQ25896_REG11_VBUSV_SHIFT                0u
#define BQ25896_REG11_VBUSV_MASK                 (0x7Fu << BQ25896_REG11_VBUSV_SHIFT)

/* REG12 Charge Current ADC */
#define BQ25896_REG12_ICHGR_SHIFT                0u
#define BQ25896_REG12_ICHGR_MASK                 (0x7Fu << BQ25896_REG12_ICHGR_SHIFT)

/* REG13 Input Current Limit Status */
#define BQ25896_REG13_VDPM_STAT_SHIFT            7u
#define BQ25896_REG13_VDPM_STAT_MASK             (0x01u << BQ25896_REG13_VDPM_STAT_SHIFT)
#define BQ25896_REG13_IDPM_STAT_SHIFT            6u
#define BQ25896_REG13_IDPM_STAT_MASK             (0x01u << BQ25896_REG13_IDPM_STAT_SHIFT)
#define BQ25896_REG13_IDPM_LIM_SHIFT             0u
#define BQ25896_REG13_IDPM_LIM_MASK              (0x3Fu << BQ25896_REG13_IDPM_LIM_SHIFT)

/* REG14 Device Information */
#define BQ25896_REG14_REG_RST_SHIFT              7u
#define BQ25896_REG14_REG_RST_MASK               (0x01u << BQ25896_REG14_REG_RST_SHIFT)
#define BQ25896_REG14_ICO_OPTIMIZED_SHIFT        6u
#define BQ25896_REG14_ICO_OPTIMIZED_MASK         (0x01u << BQ25896_REG14_ICO_OPTIMIZED_SHIFT)
#define BQ25896_REG14_PN_SHIFT                   3u
#define BQ25896_REG14_PN_MASK                    (0x07u << BQ25896_REG14_PN_SHIFT)
#define BQ25896_REG14_TS_PROFILE_SHIFT           2u
#define BQ25896_REG14_TS_PROFILE_MASK            (0x01u << BQ25896_REG14_TS_PROFILE_SHIFT)
#define BQ25896_REG14_DEV_REV_SHIFT              0u
#define BQ25896_REG14_DEV_REV_MASK               (0x03u << BQ25896_REG14_DEV_REV_SHIFT)

/* Encoded device ID values */
#define BQ25896_DEVICE_PN_BQ25896               0x00u

/* Common programmable ranges */
#define BQ25896_IINLIM_MIN_MA                   100u
#define BQ25896_IINLIM_MAX_MA                   3250u
#define BQ25896_IINLIM_STEP_MA                  50u
#define BQ25896_IINLIM_OFFSET_MA                100u
#define BQ25896_IINLIM_RAW_MAX                  0x3Fu

#define BQ25896_ICHG_MIN_MA                     0u
#define BQ25896_ICHG_MAX_MA                     3008u
#define BQ25896_ICHG_STEP_MA                    64u
#define BQ25896_ICHG_OFFSET_MA                  0u
#define BQ25896_ICHG_RAW_MAX                    0x2Fu

#define BQ25896_IPRECHG_MIN_MA                  64u
#define BQ25896_IPRECHG_MAX_MA                  1024u
#define BQ25896_IPRECHG_STEP_MA                 64u
#define BQ25896_IPRECHG_OFFSET_MA               64u
#define BQ25896_IPRECHG_RAW_MAX                 0x0Fu

#define BQ25896_ITERM_MIN_MA                    64u
#define BQ25896_ITERM_MAX_MA                    1024u
#define BQ25896_ITERM_STEP_MA                   64u
#define BQ25896_ITERM_OFFSET_MA                 64u
#define BQ25896_ITERM_RAW_MAX                   0x0Fu

#define BQ25896_SYS_MIN_MIN_MV                  3000u
#define BQ25896_SYS_MIN_MAX_MV                  3700u
#define BQ25896_SYS_MIN_STEP_MV                 100u
#define BQ25896_SYS_MIN_OFFSET_MV               3000u
#define BQ25896_SYS_MIN_RAW_MAX                 0x07u

#define BQ25896_VREG_MIN_MV                     3840u
#define BQ25896_VREG_MAX_MV                     4608u
#define BQ25896_VREG_STEP_MV                    16u
#define BQ25896_VREG_OFFSET_MV                  3840u
#define BQ25896_VREG_RAW_MAX                    0x30u

#define BQ25896_BOOSTV_MIN_MV                   4550u
#define BQ25896_BOOSTV_MAX_MV                   5510u
#define BQ25896_BOOSTV_STEP_MV                  64u
#define BQ25896_BOOSTV_OFFSET_MV                4550u
#define BQ25896_BOOSTV_RAW_MAX                  0x0Fu

/* ADC conversion scaling */
#define BQ25896_ADC_BATV_OFFSET_MV              2304u
#define BQ25896_ADC_BATV_STEP_MV                20u

#define BQ25896_ADC_SYSV_OFFSET_MV              2304u
#define BQ25896_ADC_SYSV_STEP_MV                20u

#define BQ25896_ADC_TSPCT_OFFSET_X1000          21000u
#define BQ25896_ADC_TSPCT_STEP_X1000            465u

#define BQ25896_ADC_VBUSV_OFFSET_MV             2600u
#define BQ25896_ADC_VBUSV_STEP_MV               100u

#define BQ25896_ADC_ICHGR_OFFSET_MA             0u
#define BQ25896_ADC_ICHGR_STEP_MA               50u

#define BQ25896_ADC_IDPM_LIM_OFFSET_MA          100u
#define BQ25896_ADC_IDPM_LIM_STEP_MA            50u

/* Driver timing constants */
#define BQ25896_RESET_POLL_COUNT                10u
#define BQ25896_RESET_POLL_DELAY_MS             2u
#define BQ25896_ADC_POLL_COUNT                  40u
#define BQ25896_ADC_POLL_DELAY_MS               25u

#ifdef __cplusplus
}
#endif

#endif /* BQ25896_REG_H */
