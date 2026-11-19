// Lumen HAL — native PWM driver (SPEC §5.1, §7.4).
//
// LEDC-backed: timer frequency, resolution, channel allocation, duty,
// inversion, brightness curve hook.
#pragma once

#include "i_driver.h"

#include <cstdint>

namespace lumen
{

// 16-bit duty (0 = off, 65535 = full).
using PwmDuty = uint16_t;

// A native PWM channel managed through ESP-IDF LEDC.
class IPwmDriver : public IDriver
{
  public:
    // Set duty cycle; driver validates against its resolution.
    virtual esp_err_t set_duty(PwmDuty duty) = 0;

    virtual esp_err_t get_duty(PwmDuty &duty) = 0;

    // Set timer frequency in Hz; driver validates against hardware limits.
    virtual esp_err_t set_frequency(uint32_t frequency_hz) = 0;

    virtual esp_err_t get_frequency(uint32_t &frequency_hz) = 0;

    // Set resolution in bits (1..20 for LEDC).
    virtual esp_err_t set_resolution(uint8_t resolution_bits) = 0;

    virtual esp_err_t get_resolution(uint8_t &resolution_bits) = 0;

    // Invert the output polarity.
    virtual esp_err_t set_inverted(bool inverted) = 0;

    virtual esp_err_t get_inverted(bool &inverted) = 0;

    // Set duty for a specific channel (multi-channel PWM like PCA9685).
    virtual esp_err_t set_channel_duty(uint8_t channel, PwmDuty duty) = 0;

    // Flush all pending channel updates to hardware.
    virtual esp_err_t flush() = 0;
};

} // namespace lumen
