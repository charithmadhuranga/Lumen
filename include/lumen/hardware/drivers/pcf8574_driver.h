// Lumen — PCF8574 8-bit GPIO expander driver (SPEC §7.1, P0; §7.3).
//
// 8 inputs per device, multiple devices per I2C bus, configurable active
// polarity, debouncing, change detection → input events, device health
// monitoring (read failure counter).
#pragma once

#include "esp_err.h"

#include "lumen/hardware/hal/i_i2c_bus.h"

#include <array>
#include <cstdint>
#include <vector>

namespace lumen
{

// A single input change event.
struct InputChangeEvent
{
    uint8_t device_index = 0; // which PCF8574 on the bus
    uint8_t pin = 0;          // 0..7
    bool level = false;       // resolved level (after polarity)
    uint32_t timestamp_ms = 0;
};

// PCF8574 driver. Manages one PCF8574 device on an I2C bus.
// Multiple devices = multiple driver instances sharing one I2cManager.
class Pcf8574Driver final
{
  public:
    static constexpr uint8_t kPinCount = 8;
    static constexpr uint32_t kDefaultDebounceMs = 20;

    Pcf8574Driver(II2CBus &bus, uint8_t device_addr, uint8_t device_index,
                  bool active_low = true, uint32_t debounce_ms = kDefaultDebounceMs);

    // Read raw pin states and run debounce/change detection. `now_ms` is the
    // current uptime. Returns events for pins that changed (after debounce).
    // `events` receives the events; `event_count` is set to the number written
    // (capped at max_events).
    esp_err_t poll(uint32_t now_ms, InputChangeEvent *events,
                   size_t max_events, size_t &event_count);

    // Current resolved level of a pin (after polarity).
    esp_err_t get_level(uint8_t pin, bool &level) const;

    // Health: number of consecutive read failures.
    uint32_t read_failure_count() const;
    bool healthy() const;

    uint8_t device_index() const { return device_index_; }
    uint8_t device_addr() const { return device_addr_; }

  private:
    II2CBus &bus_;
    uint8_t device_addr_;
    uint8_t device_index_;
    bool active_low_;
    uint32_t debounce_ms_;

    // Raw last-read byte and resolved levels.
    uint8_t last_raw_ = 0;
    std::array<bool, kPinCount> level_{};
    std::array<bool, kPinCount> stable_{}; // debounced stable level

    // Debounce state per pin.
    std::array<uint32_t, kPinCount> change_since_ms_{};
    std::array<bool, kPinCount> pending_change_{};

    uint32_t read_failure_count_ = 0;
    bool healthy_ = true;

    esp_err_t read_byte(uint8_t &byte);
};

} // namespace lumen
