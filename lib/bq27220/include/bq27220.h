#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "driver/i2c_master.h"

#include "bq27220_data_memory.h"
#include "bq27220_def.h"

typedef union ControlStatus {
    struct __reg
    {
        uint8_t BATT_ID : 3;
        bool SNOOZE : 1;
        bool BCA : 1;
        bool CCA : 1;
        uint8_t RSVD0 : 2;
        uint8_t RSVD1;
    } reg;
    uint16_t full;
} BQ27220ControlStatus;

typedef union BatteryStatus {
    struct __reg
    {
        uint16_t DSG : 1;
        uint16_t SYSDWN : 1;
        uint16_t TDA : 1;
        uint16_t BATTPRES : 1;
        uint16_t AUTH_GD : 1;
        uint16_t OCVGD : 1;
        uint16_t TCA : 1;
        uint16_t RSVD : 1;
        uint16_t CHGINH : 1;
        uint16_t FC : 1;
        uint16_t OTD : 1;
        uint16_t OTC : 1;
        uint16_t SLEEP : 1;
        uint16_t OCVFAIL : 1;
        uint16_t OCVCOMP : 1;
        uint16_t FD : 1;
    } reg;
    uint16_t full;
} BQ27220BatteryStatus;

typedef enum {
    Bq27220OperationStatusSecSealed = 0b11,
    Bq27220OperationStatusSecUnsealed = 0b10,
    Bq27220OperationStatusSecFull = 0b01,
} Bq27220OperationStatusSec;

typedef union OperationStatus {
    struct __reg
    {
        bool CALMD : 1;
        uint8_t SEC : 2;
        bool EDV2 : 1;
        bool VDQ : 1;
        bool INITCOMP : 1;
        bool SMTH : 1;
        bool BTPINT : 1;
        uint8_t RSVD1 : 2;
        bool CFGUPDATE : 1;
        uint8_t RSVD0 : 5;
    } reg;
    uint16_t full;
} BQ27220OperationStatus;

typedef union GaugingStatus {
    struct __reg
    {
        bool FD : 1;
        bool FC : 1;
        bool TD : 1;
        bool TC : 1;
        bool RSVD0 : 1;
        bool EDV : 1;
        bool DSG : 1;
        bool CF : 1;
        uint8_t RSVD1 : 2;
        bool FCCX : 1;
        uint8_t RSVD2 : 2;
        bool EDV1 : 1;
        bool EDV2 : 1;
        bool VDQ : 1;
    } reg;
    uint16_t full;
} BQ27220GaugingStatus;

typedef enum {
    BQ27220StateSleep = 0,
    BQ27220StateFull = 1,
    BQ27220StateCharge = 2,
    BQ27220StateDischarge = 3,
    BQ27220StateRelax = 4,
} BQ27220State;

typedef struct BQ27220Snapshot {
    BQ27220BatteryStatus battery_status;
    BQ27220GaugingStatus gauging_status;
    uint16_t soc;
    uint16_t fcc_mah;
    uint16_t soh_percent;
    int16_t current_ma;
    int16_t average_current_ma;
    uint16_t voltage_mv;
    uint16_t remaining_capacity_mah;
    uint16_t temperature_dk;
    bool charging;
    bool full;
} BQ27220Snapshot;

class BQ27220 {
public:
    BQ27220();
    ~BQ27220();

    bool begin(i2c_master_bus_handle_t bus_handle,
               uint8_t address = BQ27220_I2C_ADDRESS,
               uint32_t scl_speed_hz = BQ27220_I2C_SPEED_HZ_DEFAULT,
               uint32_t scl_wait_us = BQ27220_I2C_SCL_WAIT_US_DEFAULT,
               int timeout_ms = BQ27220_I2C_TIMEOUT_MS_DEFAULT);
    void end();
    bool isReady(void) const;

    bool getIsCharging(void);
    bool getCharingFinish(void);
    bool getChargingFinish(void);

    bool parameterCheck(uint16_t address, uint32_t value, size_t size, bool update);
    bool dateMemoryCheck(const BQ27220DMData *data_memory, bool update);

    bool init(const BQ27220DMData *data_memory = gauge_data_memory);
    bool reset(void);
    bool setDefaultCapacity(uint16_t cap);
    bool setChargeParameters(uint16_t charging_current_ma,
                             uint16_t charging_voltage_mv,
                             uint16_t taper_current_ma,
                             uint16_t charge_termination_voltage_mv = 100u);

    bool sealAccess(void);
    bool unsealAccess(void);
    bool fullAccess(void);

    uint16_t getDeviceNumber(void);
    uint16_t getVoltage(void);
    int16_t getCurrent(void);
    int16_t getAverageCurrent(void);
    bool getControlStatus(BQ27220ControlStatus *ctrl_sta);
    bool getBatteryStatus(BQ27220BatteryStatus *batt_sta);
    bool getOperationStatus(BQ27220OperationStatus *oper_sta);
    bool getGaugingStatus(BQ27220GaugingStatus *gauging_sta);
    bool readSnapshot(BQ27220Snapshot *snapshot);
    static BQ27220State classifyState(const BQ27220Snapshot *snapshot,
                                      bool vbus_connected,
                                      int16_t current_threshold_ma = 20);
    static const char *stateName(BQ27220State state);
    uint16_t getTemperature(void);
    uint16_t getFullChargeCapacity(void);
    uint16_t getDesignCapacity(void);
    uint16_t getRemainingCapacity(void);
    uint16_t getStateOfCharge(void);
    uint16_t getStateOfHealth(void);
    uint16_t getChargeVoltageMax(void);
    uint16_t readRegU16(uint16_t reg);

    bool controlSubCmd(uint16_t sub_cmd);
    bool i2cReadBytes(uint8_t reg, uint8_t *dest, size_t count) const;
    bool i2cWriteBytes(uint8_t reg, const uint8_t *src, size_t count) const;

private:
    bool readRegU16Internal(uint8_t reg, uint16_t *value) const;

    i2c_master_bus_handle_t bus_handle_;
    i2c_master_dev_handle_t dev_handle_;
    uint8_t addr_;
    uint32_t scl_speed_hz_;
    uint32_t scl_wait_us_;
    int timeout_ms_;
};
