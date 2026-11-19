// Lumen — I2C bus manager (SPEC §7.1, P0).
//
// Single owner of I2C bus(es). Lighting channels never touch I2C directly
// (AGENTS.md §6); PCA9685/PCF8574 drivers use this bus. Provides a
// transaction API with timeout/retry, error recovery (bus reset), and a scan
// endpoint for diagnostics.
#pragma once

#include "esp_err.h"

#include "lumen/hardware/hal/i_i2c_bus.h"

#include <cstdint>

namespace lumen
{

// Handle to an I2C device on a managed bus. Opaque to callers.
struct I2cDeviceHandle;

// Config for a managed I2C bus.
struct I2cBusConfig
{
    int port = 0;
    int sda_gpio = -1;
    int scl_gpio = -1;
    uint32_t frequency_hz = 100000;
};

class I2cManager final : public II2CBus
{
  public:
    I2cManager();

    // IDriver
    esp_err_t initialize() override;
    esp_err_t shutdown() override;
    DriverState state() const override;
    const char *name() const override;
    DriverType driver_type() const override { return DriverType::kI2c; }

    // II2CBus
    esp_err_t write(uint8_t device_addr, const uint8_t *write_buf,
                    size_t write_len) override;
    esp_err_t read(uint8_t device_addr, uint8_t *read_buf,
                   size_t read_len) override;
    esp_err_t write_then_read(uint8_t device_addr,
                              const uint8_t *write_buf, size_t write_len,
                              uint8_t *read_buf, size_t read_len) override;
    esp_err_t probe(uint8_t device_addr) override;
    esp_err_t scan(uint8_t *found, size_t max_found,
                   size_t &found_count) override;
    esp_err_t bus_reset() override;

    // Add a device to the bus (returns an opaque handle).
    esp_err_t add_device(uint8_t device_addr, I2cDeviceHandle **handle);

    // Remove a device.
    esp_err_t remove_device(I2cDeviceHandle *handle);

  private:
    int port_;
    int sda_gpio_;
    int scl_gpio_;
    uint32_t frequency_hz_;
    void *bus_handle_ = nullptr; // i2c_master_bus_handle_t
    DriverState state_ = DriverState::kUninitialized;
};

} // namespace lumen
