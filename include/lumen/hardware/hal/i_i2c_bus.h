// Lumen HAL — I2C bus (SPEC §5.1).
//
// The I2C manager is the single owner of I2C bus(es). Lighting channels and
// other clients never touch I2C directly (AGENTS.md §6); they go through the
// PCA9685/PCF8574 drivers which use this bus via the manager.
#pragma once

#include "i_driver.h"

#include <cstdint>

namespace lumen
{

// Result of a bus scan: device addresses found.
class II2CBus : public IDriver
{
  public:
    // Write `write_len` bytes, then (optionally) read `read_len` bytes into
    // `read_buf`. Combined transactions (write-then-read without stop) are
    // supported by passing both.
    virtual esp_err_t write(uint8_t device_addr, const uint8_t *write_buf,
                            size_t write_len) = 0;

    virtual esp_err_t read(uint8_t device_addr, uint8_t *read_buf,
                           size_t read_len) = 0;

    virtual esp_err_t write_then_read(uint8_t device_addr,
                                      const uint8_t *write_buf,
                                      size_t write_len, uint8_t *read_buf,
                                      size_t read_len) = 0;

    // Probe whether a device acknowledges at `device_addr`. Returns
    // ESP_OK if present, ESP_ERR_NOT_FOUND otherwise.
    virtual esp_err_t probe(uint8_t device_addr) = 0;

    // Scan all 127 addresses and report which acknowledge. `found` receives
    // the addresses; `found_count` is set to the number written (capped at
    // max_found).
    virtual esp_err_t scan(uint8_t *found, size_t max_found,
                           size_t &found_count) = 0;

    // Reset/recover the bus after an error (clock stretching / reset).
    virtual esp_err_t bus_reset() = 0;
};

} // namespace lumen
