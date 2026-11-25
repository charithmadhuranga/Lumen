// Lumen — native PWM driver (ESP-IDF LEDC). SPEC §7.4.
//
// One instance per PWM resource in the board profile ("type": "pwm",
// "gpio": N). Channels are allocated from the global LEDC pool; timers are
// shared between channels running at the same frequency/resolution (the
// ESP32 has 4 low-speed timers but 8 channels).
//
// Duty contract (IPwmDriver): 16-bit normalised, 0 = off, 65535 = full on.
// The driver scales it to the configured timer resolution.
#pragma once

#include "esp_err.h"
#include "lumen/hardware/hal/i_pwm_driver.h"

#include <cstdint>

namespace lumen
{

class LedcDriver final : public IPwmDriver
{
  public:
    LedcDriver(int32_t gpio, uint32_t frequency_hz = 1000,
               uint8_t resolution_bits = 13, bool inverted = false);
    ~LedcDriver() override;

    esp_err_t initialize() override;
    esp_err_t shutdown() override;
    DriverState state() const override;
    const char *name() const override;
    DriverType driver_type() const override { return DriverType::kPwm; }

    esp_err_t set_duty(PwmDuty duty) override;
    esp_err_t get_duty(PwmDuty &duty) override;
    esp_err_t set_frequency(uint32_t frequency_hz) override;
    esp_err_t get_frequency(uint32_t &frequency_hz) override;
    esp_err_t set_resolution(uint8_t resolution_bits) override;
    esp_err_t get_resolution(uint8_t &resolution_bits) override;
    esp_err_t set_inverted(bool inverted) override;
    esp_err_t get_inverted(bool &inverted) override;

    // Single-channel device: channel 0 is the only valid channel.
    esp_err_t set_channel_duty(uint8_t channel, PwmDuty duty) override;
    esp_err_t flush() override { return ESP_OK; } // LEDC updates immediately

    int32_t gpio() const { return gpio_; }
    int channel() const { return channel_; }

  private:
    int32_t gpio_;
    uint32_t frequency_hz_;
    uint8_t resolution_bits_;
    bool inverted_;
    int channel_ = -1;
    int timer_ = -1;
    PwmDuty duty_ = 0;
    DriverState state_ = DriverState::kUninitialized;

    esp_err_t write_raw(PwmDuty duty);
};

} // namespace lumen
