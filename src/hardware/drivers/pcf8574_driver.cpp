// Lumen — PCF8574 driver implementation (ESP-IDF). See pcf8574_driver.h
#include "lumen/hardware/drivers/pcf8574_driver.h"

namespace lumen
{

Pcf8574Driver::Pcf8574Driver(II2CBus &bus, uint8_t device_addr,
                             uint8_t device_index, bool active_low,
                             uint32_t debounce_ms)
    : bus_(bus),
      device_addr_(device_addr),
      device_index_(device_index),
      active_low_(active_low),
      debounce_ms_(debounce_ms)
{
    level_.fill(false);
    stable_.fill(false);
    change_since_ms_.fill(0);
    pending_change_.fill(false);
}

esp_err_t Pcf8574Driver::read_byte(uint8_t &byte)
{
    return bus_.read(device_addr_, &byte, 1);
}

esp_err_t Pcf8574Driver::poll(uint32_t now_ms, InputChangeEvent *events,
                              size_t max_events, size_t &event_count)
{
    event_count = 0;
    if (max_events == 0 || !events) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t raw = 0;
    esp_err_t err = read_byte(raw);
    if (err != ESP_OK) {
        ++read_failure_count_;
        healthy_ = false;
        return err;
    }
    read_failure_count_ = 0;
    healthy_ = true;

    last_raw_ = raw;

    for (uint8_t pin = 0; pin < kPinCount; ++pin) {
        bool raw_bit = (raw >> pin) & 1U;
        // Resolve level: active_low inverts.
        bool new_level = active_low_ ? !raw_bit : raw_bit;

        bool currently_stable = stable_[pin];
        if (new_level != currently_stable) {
            // Potential change: start/restart debounce.
            if (!pending_change_[pin]) {
                pending_change_[pin] = true;
                change_since_ms_[pin] = now_ms;
            }
            // Commit once the debounce period has elapsed (debounce_ms=0
            // commits immediately on the first poll).
            if (pending_change_[pin] &&
                now_ms - change_since_ms_[pin] >= debounce_ms_) {
                stable_[pin] = new_level;
                level_[pin] = new_level;
                pending_change_[pin] = false;
                change_since_ms_[pin] = 0;

                if (event_count < max_events) {
                    events[event_count].device_index = device_index_;
                    events[event_count].pin = pin;
                    events[event_count].level = new_level;
                    events[event_count].timestamp_ms = now_ms;
                    ++event_count;
                }
            }
        } else {
            // No change: clear pending.
            pending_change_[pin] = false;
            change_since_ms_[pin] = 0;
            level_[pin] = new_level;
        }
    }

    return ESP_OK;
}

esp_err_t Pcf8574Driver::get_level(uint8_t pin, bool &level) const
{
    if (pin >= kPinCount) {
        return ESP_ERR_INVALID_ARG;
    }
    level = level_[pin];
    return ESP_OK;
}

uint32_t Pcf8574Driver::read_failure_count() const
{
    return read_failure_count_;
}

bool Pcf8574Driver::healthy() const
{
    return healthy_;
}

} // namespace lumen
