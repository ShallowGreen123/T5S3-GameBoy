#include "bq27220_data_memory.h"

const BQ27220DMGaugingConfig data_memory_gauging_config = {
    .CCT = 1,
    .CSYNC = 1,
    .EDV_CMP = 0,
    .SC = 1,
    .FIXED_EDV0 = 1,
    .FCC_LIM = 1,
    .FC_FOR_VDQ = 1,
    .IGNORE_SD = 1,
    .SME0 = 0,
};

BQ27220DMData gauge_data_memory[] = {
    {
        .address = BQ27220DMAddressGasGaugingCEDVProfile1GaugingConfig,
        .type = BQ27220DMTypePtr16,
        .value.ptr = (uintptr_t)&data_memory_gauging_config,
    },
    {
        .address = BQ27220DMAddressConfigurationChargeChargingCurrent,
        .type = BQ27220DMTypeU16,
        .value.u16 = 512,
    },
    {
        .address = BQ27220DMAddressConfigurationChargeChargingVoltage,
        .type = BQ27220DMTypeU16,
        .value.u16 = 4208,
    },
    {
        .address = BQ27220DMAddressConfigurationChargeTerminationTaperCurrent,
        .type = BQ27220DMTypeU16,
        .value.u16 = 64,
    },
    {
        .address = BQ27220DMAddressConfigurationRegistersOperationConfigA,
        .type = BQ27220DMTypeU16,
        .value.u16 = 0x0C8C,
    },
    {
        .address = BQ27220DMAddressConfigurationRegistersOperationConfigB,
        .type = BQ27220DMTypeU8,
        .value.u8 = 0x4C,
    },
    {
        .address = BQ27220DMAddressGasGaugingCEDVProfile1FullChargeCapacity,
        .type = BQ27220DMTypeU16,
        .value.u16 = 1500,
    },
    {
        .address = BQ27220DMAddressGasGaugingCEDVProfile1DesignCapacity,
        .type = BQ27220DMTypeU16,
        .value.u16 = 1500,
    },
    {
        .address = BQ27220DMAddressGasGaugingCEDVProfile1ChargeTerminationVoltage,
        .type = BQ27220DMTypeU16,
        .value.u16 = 100,
    },
    {
        .address = BQ27220DMAddressGasGaugingCEDVProfile1EMF,
        .type = BQ27220DMTypeU16,
        .value.u16 = 3743,
    },
    {
        .address = BQ27220DMAddressGasGaugingCEDVProfile1C0,
        .type = BQ27220DMTypeU16,
        .value.u16 = 149,
    },
    {
        .address = BQ27220DMAddressGasGaugingCEDVProfile1R0,
        .type = BQ27220DMTypeU16,
        .value.u16 = 867,
    },
    {
        .address = BQ27220DMAddressGasGaugingCEDVProfile1T0,
        .type = BQ27220DMTypeU16,
        .value.u16 = 4030,
    },
    {
        .address = BQ27220DMAddressGasGaugingCEDVProfile1R1,
        .type = BQ27220DMTypeU16,
        .value.u16 = 316,
    },
    {
        .address = BQ27220DMAddressGasGaugingCEDVProfile1TC,
        .type = BQ27220DMTypeU8,
        .value.u8 = 9,
    },
    {
        .address = BQ27220DMAddressGasGaugingCEDVProfile1C1,
        .type = BQ27220DMTypeU8,
        .value.u8 = 0,
    },
    {
        .address = BQ27220DMAddressGasGaugingCEDVProfile1StartDOD0,
        .type = BQ27220DMTypeU16,
        .value.u16 = 4183,
    },
    {
        .address = BQ27220DMAddressGasGaugingCEDVProfile1StartDOD10,
        .type = BQ27220DMTypeU16,
        .value.u16 = 4043,
    },
    {
        .address = BQ27220DMAddressGasGaugingCEDVProfile1StartDOD20,
        .type = BQ27220DMTypeU16,
        .value.u16 = 3925,
    },
    {
        .address = BQ27220DMAddressGasGaugingCEDVProfile1StartDOD30,
        .type = BQ27220DMTypeU16,
        .value.u16 = 3821,
    },
    {
        .address = BQ27220DMAddressGasGaugingCEDVProfile1StartDOD40,
        .type = BQ27220DMTypeU16,
        .value.u16 = 3725,
    },
    {
        .address = BQ27220DMAddressGasGaugingCEDVProfile1StartDOD50,
        .type = BQ27220DMTypeU16,
        .value.u16 = 3665,
    },
    {
        .address = BQ27220DMAddressGasGaugingCEDVProfile1StartDOD60,
        .type = BQ27220DMTypeU16,
        .value.u16 = 3619,
    },
    {
        .address = BQ27220DMAddressGasGaugingCEDVProfile1StartDOD70,
        .type = BQ27220DMTypeU16,
        .value.u16 = 3585,
    },
    {
        .address = BQ27220DMAddressGasGaugingCEDVProfile1StartDOD80,
        .type = BQ27220DMTypeU16,
        .value.u16 = 3515,
    },
    {
        .address = BQ27220DMAddressGasGaugingCEDVProfile1StartDOD90,
        .type = BQ27220DMTypeU16,
        .value.u16 = 3439,
    },
    {
        .address = BQ27220DMAddressGasGaugingCEDVProfile1StartDOD100,
        .type = BQ27220DMTypeU16,
        .value.u16 = 3299,
    },
    {
        .address = BQ27220DMAddressGasGaugingCEDVProfile1EDV0,
        .type = BQ27220DMTypeU16,
        .value.u16 = 3300,
    },
    {
        .address = BQ27220DMAddressGasGaugingCEDVProfile1EDV1,
        .type = BQ27220DMTypeU16,
        .value.u16 = 3321,
    },
    {
        .address = BQ27220DMAddressGasGaugingCEDVProfile1EDV2,
        .type = BQ27220DMTypeU16,
        .value.u16 = 3355,
    },
    {
        .address = BQ27220DMAddressCalibrationCurrentDeadband,
        .type = BQ27220DMTypeU8,
        .value.u8 = 1,
    },
    {
        .address = BQ27220DMAddressConfigurationPowerSleepCurrent,
        .type = BQ27220DMTypeI16,
        .value.i16 = 1,
    },
    {
        .type = BQ27220DMTypeEnd,
    },
};

uint16_t get_gauge_data_memory_len(void)
{
    return (uint16_t)(sizeof(gauge_data_memory) / sizeof(gauge_data_memory[0]));
}
