// Lumen — I2C bus manager (ESP-IDF i2c_master driver).
//
// One instance per physical I2C controller declared in the board profile
// (resource type "i2c", pins {"sda","scl"}). Thread-safe: the lighting task
// (PCA9685 flushes), the input service (PCF8574 polls) and the web API (bus
// scan) share the bus, so every transaction is serialised by an internal
// mutex. Device handles are created once per address and cached — no
// add/remove per transaction on the hot path.
#pragma once

#include "esp_err.h"
#include "lumen/hardware/hal/i_i2c_bus.h"

#include <array>
#include <cstdint>
#include <mutex>

namespace lumen
{

struct I2cBusConfig
{
    int port = 0;
    int sda_gpio = -1;
    int scl_gpio = -1;
    uint32_t frequency_hz = 100000;
    uint32_t timeout_ms = 50; // per transaction
};

class I2cManager final : public II2CBus
{
  public:
    static constexpr std::size_t kMaxDevices = 16;

    explicit I2cManager(const I2cBusConfig &config);
    ~I2cManager() override;

    esp_err_t initialize() override;
    esp_err_t shutdown() override;
    DriverState state() const override;
    const char *name() const override;
    DriverType driver_type() const override { return DriverType::kI2c; }

    esp_err_t write(uint8_t device_addr, const uint8_t *write_buf,
                    size_t write_len) override;
    esp_err_t read(uint8_t device_addr, uint8_t *read_buf,
                   size_t read_len) override;
    esp_err_t write_then_read(uint8_t device_addr, const uint8_t *write_buf,
                              size_t write_len, uint8_t *read_buf,
                              size_t read_len) override;
    esp_err_t probe(uint8_t device_addr) override;
    esp_err_t scan(uint8_t *found, size_t max_found,
                   size_t &found_count) override;
    esp_err_t bus_reset() override;

    const I2cBusConfig &config() const { return config_; }
    uint32_t error_count() const { return error_count_; }

#ifdef LUMEN_TEST_HOOKS
    // Reliability test images only: every transaction to `address` fails
    // with `err` (as if the chip were unplugged). ESP_OK clears it.
    void inject_fault(uint8_t address, esp_err_t err)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        fault_address_ = address;
        fault_err_ = err;
    }
#endif

  private:
#ifdef LUMEN_TEST_HOOKS
    uint8_t fault_address_ = 0xFF;
    esp_err_t fault_err_ = ESP_OK;
#endif
    struct DeviceSlot
    {
        uint8_t address = 0;
        void *handle = nullptr; // i2c_master_dev_handle_t
    };

    // Returns the cached device handle (creating it) — lock must be held.
    void *device_locked(uint8_t address);
    void remove_all_devices_locked();

    I2cBusConfig config_;
    void *bus_handle_ = nullptr; // i2c_master_bus_handle_t
    DriverState state_ = DriverState::kUninitialized;
    std::array<DeviceSlot, kMaxDevices> devices_{};
    uint32_t error_count_ = 0;
    mutable std::mutex mutex_;
};

} // namespace lumen
