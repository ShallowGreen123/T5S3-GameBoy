#include "bq27220.h"

#include <inttypes.h>
#include <string.h>

#include "esp_err.h"
#include "esp_log.h"
#include "esp_rom_sys.h"

namespace {

constexpr char TAG[] = "BQ27220";
constexpr uint32_t BQ27220_MAC_WRITE_DELAY_US = 250u;
constexpr uint32_t BQ27220_SELECT_DELAY_US = 1000u;
constexpr uint32_t BQ27220_MAGIC_DELAY_US = 5000u;
constexpr uint32_t BQ27220_CONFIG_DELAY_US = 10000u;
constexpr uint32_t BQ27220_CONFIG_APPLY_US = 2000000u;
constexpr uint32_t BQ27220_TIMEOUT_COMMON_US = 2000000u;
constexpr uint32_t BQ27220_TIMEOUT_RESET_US = 4000000u;
constexpr uint32_t BQ27220_TIMEOUT_CYCLE_INTERVAL_US = 1000u;
constexpr uint32_t BQ27220_DELAY_CHUNK_US = 1000000u;
constexpr uint16_t BQ27220_DM_CHARGING_CURRENT_MAX_MA = 1000u;
constexpr uint16_t BQ27220_DM_CHARGING_VOLTAGE_MAX_MV = 4600u;
constexpr uint16_t BQ27220_DM_TAPER_CURRENT_MAX_MA = 1000u;
constexpr uint16_t BQ27220_DM_CHARGE_TERM_VOLTAGE_MAX_MV = 1000u;

constexpr uint32_t bq27220_timeout_cycles(uint32_t timeout_us)
{
    return timeout_us / BQ27220_TIMEOUT_CYCLE_INTERVAL_US;
}

uint8_t bq27220_get_checksum(const uint8_t *data, size_t len)
{
    uint8_t ret = 0;
    for (size_t i = 0; i < len; ++i) {
        ret = (uint8_t)(ret + data[i]);
    }
    return (uint8_t)(0xFFu - ret);
}

void bq27220_delay_us(uint32_t delay_us)
{
    while (delay_us > BQ27220_DELAY_CHUNK_US) {
        esp_rom_delay_us(BQ27220_DELAY_CHUNK_US);
        delay_us -= BQ27220_DELAY_CHUNK_US;
    }

    if (delay_us > 0u) {
        esp_rom_delay_us(delay_us);
    }
}

void bq27220_store_le(uint8_t *data, uint32_t value, size_t size)
{
    for (size_t i = 0; i < size; ++i) {
        data[i] = (uint8_t)((value >> (i * 8u)) & 0xFFu);
    }
}

void bq27220_store_be(uint8_t *data, uint32_t value, size_t size)
{
    for (size_t i = 0; i < size; ++i) {
        const size_t shift = (size - 1u - i) * 8u;
        data[i] = (uint8_t)((value >> shift) & 0xFFu);
    }
}

uint32_t bq27220_load_le(const uint8_t *data, size_t size)
{
    uint32_t value = 0;
    for (size_t i = 0; i < size; ++i) {
        value |= ((uint32_t)data[i]) << (i * 8u);
    }
    return value;
}

uint32_t bq27220_load_be(const uint8_t *data, size_t size)
{
    uint32_t value = 0;
    for (size_t i = 0; i < size; ++i) {
        value = (value << 8u) | data[i];
    }
    return value;
}

bool bq27220_resolve_dm_pointer(const BQ27220DMData *data_memory, size_t size, uint32_t *value)
{
    if ((data_memory == nullptr) || (value == nullptr) || (data_memory->value.ptr == 0u)) {
        return false;
    }

    const uint8_t *ptr = reinterpret_cast<const uint8_t *>(data_memory->value.ptr);
    *value = bq27220_load_le(ptr, size);
    return true;
}

}  // namespace

BQ27220::BQ27220()
    : bus_handle_(nullptr),
      dev_handle_(nullptr),
      addr_(BQ27220_I2C_ADDRESS),
      scl_speed_hz_(BQ27220_I2C_SPEED_HZ_DEFAULT),
      scl_wait_us_(BQ27220_I2C_SCL_WAIT_US_DEFAULT),
      timeout_ms_(BQ27220_I2C_TIMEOUT_MS_DEFAULT)
{
}

BQ27220::~BQ27220()
{
    end();
}

bool BQ27220::begin(i2c_master_bus_handle_t bus_handle,
                    uint8_t address,
                    uint32_t scl_speed_hz,
                    uint32_t scl_wait_us,
                    int timeout_ms)
{
    if (bus_handle == nullptr) {
        ESP_LOGE(TAG, "bus_handle is null");
        return false;
    }

    end();

    i2c_device_config_t device_config = {};
    device_config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    device_config.device_address = address;
    device_config.scl_speed_hz =
        (scl_speed_hz == 0u) ? BQ27220_I2C_SPEED_HZ_DEFAULT : scl_speed_hz;
    device_config.scl_wait_us = scl_wait_us;

    esp_err_t err = i2c_master_bus_add_device(bus_handle, &device_config, &dev_handle_);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2c_master_bus_add_device failed: %s", esp_err_to_name(err));
        dev_handle_ = nullptr;
        return false;
    }

    bus_handle_ = bus_handle;
    addr_ = address;
    scl_speed_hz_ = device_config.scl_speed_hz;
    scl_wait_us_ = device_config.scl_wait_us;
    timeout_ms_ = (timeout_ms > 0) ? timeout_ms : BQ27220_I2C_TIMEOUT_MS_DEFAULT;

    const uint16_t device_number = getDeviceNumber();
    if (device_number != BQ27220_DEVICE_ID) {
        ESP_LOGE(TAG,
                 "unexpected device id 0x%04x at 0x%02x, expected 0x%04x",
                 device_number,
                 addr_,
                 BQ27220_DEVICE_ID);
        end();
        return false;
    }

    return true;
}

void BQ27220::end()
{
    if (dev_handle_ != nullptr) {
        esp_err_t err = i2c_master_bus_rm_device(dev_handle_);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "i2c_master_bus_rm_device failed: %s", esp_err_to_name(err));
        }
    }

    bus_handle_ = nullptr;
    dev_handle_ = nullptr;
    addr_ = BQ27220_I2C_ADDRESS;
    scl_speed_hz_ = BQ27220_I2C_SPEED_HZ_DEFAULT;
    scl_wait_us_ = BQ27220_I2C_SCL_WAIT_US_DEFAULT;
    timeout_ms_ = BQ27220_I2C_TIMEOUT_MS_DEFAULT;
}

bool BQ27220::isReady(void) const
{
    return dev_handle_ != nullptr;
}

bool BQ27220::getIsCharging(void)
{
    BQ27220Snapshot snapshot = {};
    if (!readSnapshot(&snapshot)) {
        return false;
    }
    return snapshot.charging;
}

bool BQ27220::getCharingFinish(void)
{
    BQ27220Snapshot snapshot = {};
    if (!readSnapshot(&snapshot)) {
        return false;
    }

    return snapshot.full;
}

bool BQ27220::getChargingFinish(void)
{
    return getCharingFinish();
}

bool BQ27220::parameterCheck(uint16_t address, uint32_t value, size_t size, bool update)
{
    if ((size != 1u) && (size != 2u) && (size != 4u)) {
        ESP_LOGE(TAG, "invalid parameter size: %u", (unsigned)size);
        return false;
    }

    bool ret = false;
    uint8_t buffer[6] = {0};
    uint8_t old_data[4] = {0};

    do {
        buffer[0] = (uint8_t)(address & 0xFFu);
        buffer[1] = (uint8_t)((address >> 8) & 0xFFu);
        bq27220_store_be(&buffer[2], value, size);

        if (update) {
            if (!i2cWriteBytes(CommandSelectSubclass, buffer, size + 2u)) {
                ESP_LOGE(TAG, "DM write failed at 0x%04x", address);
                break;
            }

            bq27220_delay_us(BQ27220_MAC_WRITE_DELAY_US);

            const uint8_t checksum = bq27220_get_checksum(buffer, size + 2u);
            uint8_t checksum_buf[2] = {
                checksum,
                (uint8_t)(2u + size + 1u + 1u),
            };
            if (!i2cWriteBytes(CommandMACDataSum, checksum_buf, sizeof(checksum_buf))) {
                ESP_LOGE(TAG, "CRC write failed at 0x%04x", address);
                break;
            }

            bq27220_delay_us(BQ27220_CONFIG_DELAY_US);
            ret = true;
        } else {
            if (!i2cWriteBytes(CommandSelectSubclass, buffer, 2u)) {
                ESP_LOGE(TAG, "DM SelectSubclass for read failed at 0x%04x", address);
                break;
            }

            bq27220_delay_us(BQ27220_SELECT_DELAY_US);

            if (!i2cReadBytes(CommandMACData, old_data, size)) {
                ESP_LOGE(TAG, "DM read failed at 0x%04x", address);
                break;
            }

            bq27220_delay_us(BQ27220_SELECT_DELAY_US);

            if (memcmp(old_data, &buffer[2], size) != 0) {
                ESP_LOGW(TAG,
                         "Data mismatch at 0x%04x (%u): 0x%08" PRIx32 " != 0x%08" PRIx32,
                         address,
                         (unsigned)size,
                         bq27220_load_be(old_data, size),
                         value);
            } else {
                ret = true;
            }
        }
    } while (false);

    return ret;
}

bool BQ27220::dateMemoryCheck(const BQ27220DMData *data_memory, bool update)
{
    if (data_memory == nullptr) {
        ESP_LOGE(TAG, "data_memory is null");
        return false;
    }

    if (update) {
        uint8_t cfg_request[2] = {0};
        bq27220_store_le(cfg_request, Control_ENTER_CFG_UPDATE, sizeof(cfg_request));
        if (!i2cWriteBytes(CommandSelectSubclass, cfg_request, sizeof(cfg_request))) {
            ESP_LOGE(TAG, "ENTER_CFG_UPDATE command failed");
            return false;
        }

        uint32_t timeout = bq27220_timeout_cycles(BQ27220_TIMEOUT_COMMON_US);
        BQ27220OperationStatus operation_status = {};
        while (--timeout > 0u) {
            if (!getOperationStatus(&operation_status)) {
                ESP_LOGW(TAG, "failed to get operation status, retries left %" PRIu32, timeout);
            } else if (operation_status.reg.CFGUPDATE) {
                break;
            }
            bq27220_delay_us(BQ27220_TIMEOUT_CYCLE_INTERVAL_US);
        }

        if (timeout == 0u) {
            ESP_LOGE(TAG,
                     "Enter CFGUPDATE mode failed, CFG=%u SEC=%u",
                     operation_status.reg.CFGUPDATE,
                     operation_status.reg.SEC);
            return false;
        }
    }

    bool result = true;
    while (data_memory->type != BQ27220DMTypeEnd) {
        uint32_t pointer_value = 0;
        switch (data_memory->type) {
            case BQ27220DMTypeWait:
                bq27220_delay_us(data_memory->value.u32);
                break;
            case BQ27220DMTypeU8:
                result &= parameterCheck(data_memory->address, data_memory->value.u8, 1u, update);
                break;
            case BQ27220DMTypeU16:
                result &= parameterCheck(data_memory->address, data_memory->value.u16, 2u, update);
                break;
            case BQ27220DMTypeU32:
                result &= parameterCheck(data_memory->address, data_memory->value.u32, 4u, update);
                break;
            case BQ27220DMTypeI8:
                result &= parameterCheck(data_memory->address,
                                         (uint32_t)(uint8_t)data_memory->value.i8,
                                         1u,
                                         update);
                break;
            case BQ27220DMTypeI16:
                result &= parameterCheck(data_memory->address,
                                         (uint32_t)(uint16_t)data_memory->value.i16,
                                         2u,
                                         update);
                break;
            case BQ27220DMTypeI32:
                result &= parameterCheck(data_memory->address,
                                         (uint32_t)data_memory->value.i32,
                                         4u,
                                         update);
                break;
            case BQ27220DMTypeF32:
                result &= parameterCheck(data_memory->address, data_memory->value.u32, 4u, update);
                break;
            case BQ27220DMTypePtr8:
                if (!bq27220_resolve_dm_pointer(data_memory, 1u, &pointer_value)) {
                    ESP_LOGE(TAG, "Invalid Ptr8 data at 0x%04x", data_memory->address);
                    result = false;
                } else {
                    result &= parameterCheck(data_memory->address, pointer_value, 1u, update);
                }
                break;
            case BQ27220DMTypePtr16:
                if (!bq27220_resolve_dm_pointer(data_memory, 2u, &pointer_value)) {
                    ESP_LOGE(TAG, "Invalid Ptr16 data at 0x%04x", data_memory->address);
                    result = false;
                } else {
                    result &= parameterCheck(data_memory->address, pointer_value, 2u, update);
                }
                break;
            case BQ27220DMTypePtr32:
                if (!bq27220_resolve_dm_pointer(data_memory, 4u, &pointer_value)) {
                    ESP_LOGE(TAG, "Invalid Ptr32 data at 0x%04x", data_memory->address);
                    result = false;
                } else {
                    result &= parameterCheck(data_memory->address, pointer_value, 4u, update);
                }
                break;
            default:
                ESP_LOGE(TAG, "Invalid DM type %u", data_memory->type);
                result = false;
                break;
        }
        ++data_memory;
    }

    if (update && result) {
        if (!controlSubCmd(Control_EXIT_CFG_UPDATE_REINIT)) {
            ESP_LOGE(TAG, "EXIT_CFG_UPDATE_REINIT failed");
            return false;
        }

        bq27220_delay_us(BQ27220_CONFIG_APPLY_US);

        uint32_t timeout = bq27220_timeout_cycles(BQ27220_TIMEOUT_COMMON_US);
        BQ27220OperationStatus operation_status = {};
        while (--timeout > 0u) {
            if (!getOperationStatus(&operation_status)) {
                ESP_LOGW(TAG, "failed to get operation status, retries left %" PRIu32, timeout);
            } else if (!operation_status.reg.CFGUPDATE) {
                break;
            }
            bq27220_delay_us(BQ27220_TIMEOUT_CYCLE_INTERVAL_US);
        }

        if (timeout == 0u) {
            ESP_LOGE(TAG, "Exit CFGUPDATE mode failed");
            return false;
        }
    }

    return result;
}

bool BQ27220::init(const BQ27220DMData *data_memory)
{
    bool result = false;
    bool reset_and_provisioning_required = false;

    do {
        const uint16_t data = getDeviceNumber();
        if (data != BQ27220_DEVICE_ID) {
            ESP_LOGE(TAG, "Invalid device number 0x%04x != 0x%04x", data, BQ27220_DEVICE_ID);
            break;
        }

        if (!unsealAccess()) {
            break;
        }

        BQ27220OperationStatus operat = {};
        if (!getOperationStatus(&operat)) {
            break;
        }
        if (!operat.reg.INITCOMP || operat.reg.CFGUPDATE) {
            ESP_LOGW(TAG, "Incorrect state, reset needed");
            reset_and_provisioning_required = true;
        }

        ESP_LOGI(TAG, "Checking chosen profile");
        BQ27220ControlStatus control_status = {};
        if (!getControlStatus(&control_status)) {
            ESP_LOGE(TAG, "Failed to get control status");
            break;
        }
        if (control_status.reg.BATT_ID != 0u) {
            ESP_LOGW(TAG, "Incorrect profile, reset needed");
            reset_and_provisioning_required = true;
        }

        if (!reset_and_provisioning_required) {
            ESP_LOGI(TAG, "Checking data memory");
            if (!dateMemoryCheck(data_memory, false)) {
                ESP_LOGW(TAG, "Incorrect configuration data, reset needed");
                reset_and_provisioning_required = true;
            }
        }

        if (reset_and_provisioning_required) {
            if (!reset()) {
                ESP_LOGE(TAG, "Failed to reset device");
            }

            if (!fullAccess()) {
                break;
            }

            ESP_LOGI(TAG, "Updating data memory");
            if (!dateMemoryCheck(data_memory, true)) {
                ESP_LOGE(TAG, "Data memory update failed");
                break;
            }
            if (!dateMemoryCheck(data_memory, false)) {
                ESP_LOGE(TAG, "Data memory verification failed");
                break;
            }
        }

        if (!sealAccess()) {
            ESP_LOGE(TAG, "Seal failed");
            break;
        }
        result = true;
    } while (false);

    return result;
}

bool BQ27220::reset(void)
{
    bool result = false;

    do {
        if (!controlSubCmd(Control_RESET)) {
            ESP_LOGE(TAG, "RESET command failed");
            break;
        }

        uint32_t timeout = bq27220_timeout_cycles(BQ27220_TIMEOUT_RESET_US);
        BQ27220OperationStatus operat = {};
        while (--timeout > 0u) {
            if (!getOperationStatus(&operat)) {
                ESP_LOGW(TAG, "Failed to get operation status, retries left %" PRIu32, timeout);
            } else if (operat.reg.INITCOMP) {
                break;
            }
            bq27220_delay_us(BQ27220_TIMEOUT_CYCLE_INTERVAL_US);
        }

        if (timeout == 0u) {
            ESP_LOGE(TAG, "INITCOMP timeout after reset");
            break;
        }

        ESP_LOGI(TAG, "Reset completed, cycles left: %" PRIu32, timeout);
        result = true;
    } while (false);

    return result;
}

bool BQ27220::setDefaultCapacity(uint16_t cap)
{
    const uint16_t len = get_gauge_data_memory_len();
    bool found_fcc = false;
    bool found_design = false;

    for (uint16_t i = 0; i < len; ++i) {
        if (gauge_data_memory[i].address ==
            BQ27220DMAddressGasGaugingCEDVProfile1FullChargeCapacity) {
            gauge_data_memory[i].value.u16 = cap;
            found_fcc = true;
        }
        if (gauge_data_memory[i].address ==
            BQ27220DMAddressGasGaugingCEDVProfile1DesignCapacity) {
            gauge_data_memory[i].value.u16 = cap;
            found_design = true;
        }
    }

    if (!found_fcc || !found_design) {
        ESP_LOGE(TAG,
                 "capacity DM entries missing: FCC=%d Design=%d",
                 found_fcc ? 1 : 0,
                 found_design ? 1 : 0);
        return false;
    }

    return true;
}

bool BQ27220::setChargeParameters(uint16_t charging_current_ma,
                                  uint16_t charging_voltage_mv,
                                  uint16_t taper_current_ma,
                                  uint16_t charge_termination_voltage_mv)
{
    if (charging_current_ma > BQ27220_DM_CHARGING_CURRENT_MAX_MA) {
        ESP_LOGE(TAG, "charging_current_ma out of range: %u", charging_current_ma);
        return false;
    }
    if (charging_voltage_mv > BQ27220_DM_CHARGING_VOLTAGE_MAX_MV) {
        ESP_LOGE(TAG, "charging_voltage_mv out of range: %u", charging_voltage_mv);
        return false;
    }
    if (taper_current_ma > BQ27220_DM_TAPER_CURRENT_MAX_MA) {
        ESP_LOGE(TAG, "taper_current_ma out of range: %u", taper_current_ma);
        return false;
    }
    if (charge_termination_voltage_mv > BQ27220_DM_CHARGE_TERM_VOLTAGE_MAX_MV) {
        ESP_LOGE(TAG,
                 "charge_termination_voltage_mv out of range: %u",
                 charge_termination_voltage_mv);
        return false;
    }

    const uint16_t len = get_gauge_data_memory_len();
    bool found_charge_current = false;
    bool found_charge_voltage = false;
    bool found_taper_current = false;
    bool found_charge_term_voltage = false;

    for (uint16_t i = 0; i < len; ++i) {
        switch (gauge_data_memory[i].address) {
            case BQ27220DMAddressConfigurationChargeChargingCurrent:
                gauge_data_memory[i].value.u16 = charging_current_ma;
                found_charge_current = true;
                break;
            case BQ27220DMAddressConfigurationChargeChargingVoltage:
                gauge_data_memory[i].value.u16 = charging_voltage_mv;
                found_charge_voltage = true;
                break;
            case BQ27220DMAddressConfigurationChargeTerminationTaperCurrent:
                gauge_data_memory[i].value.u16 = taper_current_ma;
                found_taper_current = true;
                break;
            case BQ27220DMAddressGasGaugingCEDVProfile1ChargeTerminationVoltage:
                gauge_data_memory[i].value.u16 = charge_termination_voltage_mv;
                found_charge_term_voltage = true;
                break;
            default:
                break;
        }
    }

    if (!found_charge_current || !found_charge_voltage || !found_taper_current ||
        !found_charge_term_voltage) {
        ESP_LOGE(TAG,
                 "charge DM entries missing: I=%d V=%d Taper=%d TermV=%d",
                 found_charge_current ? 1 : 0,
                 found_charge_voltage ? 1 : 0,
                 found_taper_current ? 1 : 0,
                 found_charge_term_voltage ? 1 : 0);
        return false;
    }

    return true;
}

bool BQ27220::sealAccess(void)
{
    bool result = false;
    BQ27220OperationStatus operat = {};

    do {
        if (!getOperationStatus(&operat)) {
            break;
        }
        if (operat.reg.SEC == Bq27220OperationStatusSecSealed) {
            result = true;
            break;
        }

        if (!controlSubCmd(Control_SEALED)) {
            break;
        }
        bq27220_delay_us(BQ27220_SELECT_DELAY_US);

        if (!getOperationStatus(&operat)) {
            break;
        }
        if (operat.reg.SEC != Bq27220OperationStatusSecSealed) {
            ESP_LOGE(TAG, "Seal failed %u", operat.reg.SEC);
            break;
        }
        result = true;
    } while (false);

    return result;
}

bool BQ27220::unsealAccess(void)
{
    bool result = false;
    BQ27220OperationStatus operat = {};

    do {
        if (!getOperationStatus(&operat)) {
            break;
        }
        if (operat.reg.SEC != Bq27220OperationStatusSecSealed) {
            result = true;
            break;
        }

        if (!controlSubCmd(UnsealKey1)) {
            break;
        }
        bq27220_delay_us(BQ27220_MAGIC_DELAY_US);
        if (!controlSubCmd(UnsealKey2)) {
            break;
        }
        bq27220_delay_us(BQ27220_MAGIC_DELAY_US);

        if (!getOperationStatus(&operat)) {
            break;
        }
        if (operat.reg.SEC != Bq27220OperationStatusSecUnsealed) {
            ESP_LOGE(TAG, "Unseal failed %u", operat.reg.SEC);
            break;
        }
        result = true;
    } while (false);

    return result;
}

bool BQ27220::fullAccess(void)
{
    bool result = false;
    BQ27220OperationStatus operat = {};

    do {
        uint32_t timeout = bq27220_timeout_cycles(BQ27220_TIMEOUT_COMMON_US);
        while (--timeout > 0u) {
            if (!getOperationStatus(&operat)) {
                ESP_LOGW(TAG, "Failed to get operation status, retries left %" PRIu32, timeout);
            } else {
                break;
            }
        }
        if (timeout == 0u) {
            ESP_LOGE(TAG, "Failed to get operation status");
            break;
        }

        if (operat.reg.SEC == Bq27220OperationStatusSecFull) {
            result = true;
            break;
        }
        if (operat.reg.SEC != Bq27220OperationStatusSecUnsealed) {
            ESP_LOGE(TAG, "Not in unsealed state");
            break;
        }

        if (!controlSubCmd(FullAccessKey)) {
            break;
        }
        bq27220_delay_us(BQ27220_MAGIC_DELAY_US);
        if (!controlSubCmd(FullAccessKey)) {
            break;
        }
        bq27220_delay_us(BQ27220_MAGIC_DELAY_US);

        if (!getOperationStatus(&operat)) {
            ESP_LOGE(TAG, "Status query failed");
            break;
        }
        if (operat.reg.SEC != Bq27220OperationStatusSecFull) {
            ESP_LOGE(TAG, "Full access failed %u", operat.reg.SEC);
            break;
        }
        result = true;
    } while (false);

    return result;
}

uint16_t BQ27220::getDeviceNumber(void)
{
    uint16_t devid = 0;
    if (!controlSubCmd(Control_DEVICE_NUMBER)) {
        return 0;
    }

    bq27220_delay_us(BQ27220_SELECT_DELAY_US);
    if (!i2cReadBytes(CommandMACData, reinterpret_cast<uint8_t *>(&devid), sizeof(devid))) {
        return 0;
    }

    return devid;
}

uint16_t BQ27220::getVoltage(void)
{
    return readRegU16(CommandVoltage);
}

int16_t BQ27220::getCurrent(void)
{
    return (int16_t)readRegU16(CommandCurrent);
}

int16_t BQ27220::getAverageCurrent(void)
{
    return (int16_t)readRegU16(CommandAverageCurrent);
}

bool BQ27220::getControlStatus(BQ27220ControlStatus *ctrl_sta)
{
    if (ctrl_sta == nullptr) {
        return false;
    }
    return readRegU16Internal(CommandControl, &ctrl_sta->full);
}

bool BQ27220::getBatteryStatus(BQ27220BatteryStatus *batt_sta)
{
    if (batt_sta == nullptr) {
        return false;
    }
    return readRegU16Internal(CommandBatteryStatus, &batt_sta->full);
}

bool BQ27220::getOperationStatus(BQ27220OperationStatus *oper_sta)
{
    if (oper_sta == nullptr) {
        return false;
    }
    return readRegU16Internal(CommandOperationStatus, &oper_sta->full);
}

bool BQ27220::getGaugingStatus(BQ27220GaugingStatus *gauging_sta)
{
    if (gauging_sta == nullptr) {
        return false;
    }

    if (!controlSubCmd(Control_GAUGING_STATUS)) {
        return false;
    }
    bq27220_delay_us(BQ27220_SELECT_DELAY_US);
    return readRegU16Internal(CommandMACData, &gauging_sta->full);
}

bool BQ27220::readSnapshot(BQ27220Snapshot *snapshot)
{
    uint16_t current_raw = 0;
    uint16_t average_current_raw = 0;
    uint16_t state_of_health = 0;

    if (snapshot == nullptr) {
        return false;
    }

    memset(snapshot, 0, sizeof(*snapshot));

    if (!getBatteryStatus(&snapshot->battery_status) ||
        !getGaugingStatus(&snapshot->gauging_status) ||
        !readRegU16Internal(CommandStateOfCharge, &snapshot->soc) ||
        !readRegU16Internal(CommandFullChargeCapacity, &snapshot->fcc_mah) ||
        !readRegU16Internal(CommandVoltage, &snapshot->voltage_mv) ||
        !readRegU16Internal(CommandRemainingCapacity, &snapshot->remaining_capacity_mah) ||
        !readRegU16Internal(CommandStateOfHealth, &state_of_health) ||
        !readRegU16Internal(CommandTemperature, &snapshot->temperature_dk) ||
        !readRegU16Internal(CommandCurrent, &current_raw) ||
        !readRegU16Internal(CommandAverageCurrent, &average_current_raw)) {
        return false;
    }

    snapshot->current_ma = (int16_t)current_raw;
    snapshot->average_current_ma = (int16_t)average_current_raw;
    snapshot->soh_percent = (uint16_t)(state_of_health & 0x00FFu);
    snapshot->full = snapshot->battery_status.reg.FC || snapshot->gauging_status.reg.FC;
    snapshot->charging =
        !snapshot->full &&
        !snapshot->battery_status.reg.CHGINH &&
        ((snapshot->current_ma > 0) || (snapshot->average_current_ma > 0));
    return true;
}

BQ27220State BQ27220::classifyState(const BQ27220Snapshot *snapshot,
                                    bool vbus_connected,
                                    int16_t current_threshold_ma)
{
    if (snapshot == nullptr) {
        return BQ27220StateRelax;
    }

    if (snapshot->battery_status.reg.SLEEP) {
        return BQ27220StateSleep;
    }

    if (snapshot->full) {
        return BQ27220StateFull;
    }

    if (snapshot->charging &&
        vbus_connected &&
        (snapshot->average_current_ma > current_threshold_ma)) {
        return BQ27220StateCharge;
    }

    if (snapshot->battery_status.reg.DSG ||
        (snapshot->average_current_ma < -current_threshold_ma)) {
        return BQ27220StateDischarge;
    }

    return BQ27220StateRelax;
}

const char *BQ27220::stateName(BQ27220State state)
{
    switch (state) {
        case BQ27220StateSleep:
            return "Sleep";
        case BQ27220StateFull:
            return "Full";
        case BQ27220StateCharge:
            return "Charge";
        case BQ27220StateDischarge:
            return "Discharge";
        case BQ27220StateRelax:
        default:
            return "Relax";
    }
}

uint16_t BQ27220::getTemperature(void)
{
    return readRegU16(CommandTemperature);
}

uint16_t BQ27220::getFullChargeCapacity(void)
{
    return readRegU16(CommandFullChargeCapacity);
}

uint16_t BQ27220::getDesignCapacity(void)
{
    return readRegU16(CommandDesignCapacity);
}

uint16_t BQ27220::getRemainingCapacity(void)
{
    return readRegU16(CommandRemainingCapacity);
}

uint16_t BQ27220::getStateOfCharge(void)
{
    return readRegU16(CommandStateOfCharge);
}

uint16_t BQ27220::getStateOfHealth(void)
{
    return readRegU16(CommandStateOfHealth);
}

uint16_t BQ27220::getChargeVoltageMax(void)
{
    return readRegU16(CommandChargeVoltage);
}

uint16_t BQ27220::readRegU16(uint16_t reg)
{
    uint16_t value = 0;
    (void)readRegU16Internal((uint8_t)reg, &value);
    return value;
}

bool BQ27220::controlSubCmd(uint16_t sub_cmd)
{
    uint8_t buf[2] = {0};
    bq27220_store_le(buf, sub_cmd, sizeof(buf));
    return i2cWriteBytes(CommandControl, buf, sizeof(buf));
}

bool BQ27220::i2cReadBytes(uint8_t reg, uint8_t *dest, size_t count) const
{
    if ((dest == nullptr) || (count == 0u) || !isReady()) {
        return false;
    }

    esp_err_t err =
        i2c_master_transmit_receive(dev_handle_, &reg, sizeof(reg), dest, count, timeout_ms_);
    if (err != ESP_OK) {
        ESP_LOGE(
            TAG, "i2c read failed addr=0x%02x reg=0x%02x: %s", addr_, reg, esp_err_to_name(err));
        return false;
    }

    return true;
}

bool BQ27220::i2cWriteBytes(uint8_t reg, const uint8_t *src, size_t count) const
{
    if (!isReady() || ((count > 0u) && (src == nullptr))) {
        return false;
    }

    i2c_master_transmit_multi_buffer_info_t buffers[2] = {};
    buffers[0].write_buffer = &reg;
    buffers[0].buffer_size = sizeof(reg);
    buffers[1].write_buffer = const_cast<uint8_t *>(src);
    buffers[1].buffer_size = count;

    esp_err_t err = i2c_master_multi_buffer_transmit(dev_handle_, buffers, 2u, timeout_ms_);
    if (err != ESP_OK) {
        ESP_LOGE(
            TAG, "i2c write failed addr=0x%02x reg=0x%02x: %s", addr_, reg, esp_err_to_name(err));
        return false;
    }

    return true;
}

bool BQ27220::readRegU16Internal(uint8_t reg, uint16_t *value) const
{
    if (value == nullptr) {
        return false;
    }

    uint8_t data[2] = {0};
    if (!i2cReadBytes(reg, data, sizeof(data))) {
        return false;
    }

    *value = (uint16_t)(((uint16_t)data[1] << 8u) | data[0]);
    return true;
}
