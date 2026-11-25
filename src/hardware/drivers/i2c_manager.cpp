// Lumen — I2C manager implementation (ESP-IDF). See i2c_manager.h
#include "lumen/hardware/drivers/i2c_manager.h"

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "esp_rom_sys.h"

namespace lumen
{

namespace
{
const char *TAG = "i2c";

i2c_master_bus_handle_t bus(void *h)
{
    return static_cast<i2c_master_bus_handle_t>(h);
}
i2c_master_dev_handle_t dev(void *h)
{
    return static_cast<i2c_master_dev_handle_t>(h);
}
} // namespace

I2cManager::I2cManager(const I2cBusConfig &config) : config_(config) {}

I2cManager::~I2cManager()
{
    shutdown();
}

esp_err_t I2cManager::initialize()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (state_ == DriverState::kInitialized) {
        return ESP_OK;
    }
    if (config_.sda_gpio < 0 || config_.scl_gpio < 0) {
        state_ = DriverState::kFailed;
        return ESP_ERR_INVALID_ARG;
    }
    i2c_master_bus_config_t bus_config{};
    bus_config.i2c_port = static_cast<i2c_port_num_t>(config_.port);
    bus_config.sda_io_num = static_cast<gpio_num_t>(config_.sda_gpio);
    bus_config.scl_io_num = static_cast<gpio_num_t>(config_.scl_gpio);
    bus_config.clk_source = I2C_CLK_SRC_DEFAULT;
    bus_config.glitch_ignore_cnt = 7;
    bus_config.flags.enable_internal_pullup = true;

    i2c_master_bus_handle_t handle = nullptr;
    esp_err_t err = i2c_new_master_bus(&bus_config, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "bus %d init failed (sda=%d scl=%d): %s", config_.port,
                 config_.sda_gpio, config_.scl_gpio, esp_err_to_name(err));
        state_ = DriverState::kFailed;
        return err;
    }
    bus_handle_ = handle;
    state_ = DriverState::kInitialized;
    ESP_LOGI(TAG, "bus %d up (sda=%d scl=%d %lu Hz)", config_.port,
             config_.sda_gpio, config_.scl_gpio,
             static_cast<unsigned long>(config_.frequency_hz));
    return ESP_OK;
}

void I2cManager::remove_all_devices_locked()
{
    for (auto &slot : devices_) {
        if (slot.handle != nullptr) {
            i2c_master_bus_rm_device(dev(slot.handle));
            slot.handle = nullptr;
        }
    }
}

esp_err_t I2cManager::shutdown()
{
    std::lock_guard<std::mutex> lock(mutex_);
    esp_err_t err = ESP_OK;
    remove_all_devices_locked();
    if (bus_handle_ != nullptr) {
        err = i2c_del_master_bus(bus(bus_handle_));
        bus_handle_ = nullptr;
    }
    state_ = DriverState::kUninitialized;
    return err;
}

DriverState I2cManager::state() const
{
    return state_;
}

const char *I2cManager::name() const
{
    return "i2c";
}

void *I2cManager::device_locked(uint8_t address)
{
    DeviceSlot *free_slot = nullptr;
    for (auto &slot : devices_) {
        if (slot.handle != nullptr && slot.address == address) {
            return slot.handle;
        }
        if (slot.handle == nullptr && free_slot == nullptr) {
            free_slot = &slot;
        }
    }
    if (free_slot == nullptr) {
        return nullptr; // bounded device table exhausted
    }
    i2c_device_config_t cfg{};
    cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    cfg.device_address = address;
    cfg.scl_speed_hz = config_.frequency_hz;
    i2c_master_dev_handle_t handle = nullptr;
    if (i2c_master_bus_add_device(bus(bus_handle_), &cfg, &handle) != ESP_OK) {
        return nullptr;
    }
    free_slot->address = address;
    free_slot->handle = handle;
    return handle;
}

esp_err_t I2cManager::write(uint8_t device_addr, const uint8_t *write_buf,
                            size_t write_len)
{
    return write_then_read(device_addr, write_buf, write_len, nullptr, 0);
}

esp_err_t I2cManager::read(uint8_t device_addr, uint8_t *read_buf,
                           size_t read_len)
{
    return write_then_read(device_addr, nullptr, 0, read_buf, read_len);
}

esp_err_t I2cManager::write_then_read(uint8_t device_addr,
                                      const uint8_t *write_buf,
                                      size_t write_len, uint8_t *read_buf,
                                      size_t read_len)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (state_ != DriverState::kInitialized || bus_handle_ == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }
    void *handle = device_locked(device_addr);
    if (handle == nullptr) {
        ++error_count_;
        return ESP_ERR_NO_MEM;
    }
    const int timeout = static_cast<int>(config_.timeout_ms);
    esp_err_t err = ESP_OK;
    if (write_buf != nullptr && write_len > 0 && read_buf != nullptr && read_len > 0) {
        err = i2c_master_transmit_receive(dev(handle), write_buf, write_len,
                                          read_buf, read_len, timeout);
    } else if (write_buf != nullptr && write_len > 0) {
        err = i2c_master_transmit(dev(handle), write_buf, write_len, timeout);
    } else if (read_buf != nullptr && read_len > 0) {
        err = i2c_master_receive(dev(handle), read_buf, read_len, timeout);
    } else {
        err = ESP_ERR_INVALID_ARG;
    }
    if (err != ESP_OK) {
        ++error_count_;
    }
    return err;
}

esp_err_t I2cManager::probe(uint8_t device_addr)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (state_ != DriverState::kInitialized || bus_handle_ == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }
    return i2c_master_probe(bus(bus_handle_), device_addr,
                            static_cast<int>(config_.timeout_ms));
}

esp_err_t I2cManager::scan(uint8_t *found, size_t max_found,
                           size_t &found_count)
{
    found_count = 0;
    if (found == nullptr || max_found == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    // 0x00 is the general-call address (every device ACKs it) and 0x78+ are
    // reserved; scan the 7-bit user range only. The lock is taken per probe
    // so lighting flushes are not starved during a scan.
    for (uint8_t addr = 0x08; addr <= 0x77 && found_count < max_found; ++addr) {
        esp_err_t err = probe(addr);
        if (err == ESP_ERR_INVALID_STATE) {
            return err;
        }
        if (err == ESP_OK) {
            found[found_count++] = addr;
        }
    }
    return ESP_OK;
}

esp_err_t I2cManager::bus_reset()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (bus_handle_ == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }
    return i2c_master_bus_reset(bus(bus_handle_));
}

} // namespace lumen
