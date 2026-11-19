// Lumen — native LEDC PWM driver (SPEC §7.1, P0; §7.4).
//
// ESP-IDF LEDC implementation of IPwmDriver. Manages timer frequency,
// resolution, channel allocation, duty, inversion. Conflict detection via
// static tracking of allocated channels/timers.
#pragma once

#include "esp_err.h"

#include "lumen/hardware/hal/i_pwm_driver.h"

namespace lumen
{

class LedcDriver final : public IPwmDriver
{
  public:
    LedcDriver(uint8_t channel, uint8_t timer_num, uint32_t frequency_hz,
               uint8_t resolution_bits);

    // IDriver
    esp_err_t initialize() override;
    esp_err_t shutdown() override;
    DriverState state() const override;
    const char *name() const override;
    DriverType driver_type() const override { return DriverType::kPwm; }

    // IPwmDriver
    esp_err_t set_duty(PwmDuty duty) override;
    esp_err_t get_duty(PwmDuty &duty) override;
    esp_err_t set_frequency(uint32_t frequency_hz) override;
    esp_err_t get_frequency(uint32_t &frequency_hz) override;
    esp_err_t set_resolution(uint8_t resolution_bits) override;
    esp_err_t get_resolution(uint8_t &resolution_bits) override;
    esp_err_t set_inverted(bool inverted) override;
    esp_err_t get_inverted(bool &inverted) override;
    virtual esp_err_t set_channel_duty(uint8_t channel, PwmDuty duty) = 0;

    virtual esp_err_t flush() = 0;

    uint8_t channel() const { return channel_; }
    uint8_t timer_num() const { return timer_num_; }

  private:
    uint8_t channel_;
    uint8_t timer_num_;
    uint32_t frequency_hz_;
    uint8_t resolution_bits_;
    PwmDuty duty_ = 0;
    bool inverted_ = false;
    DriverState state_ = DriverState::kUninitialized;
};

} // namespace lumen
