// Lumen — LEDC PWM driver implementation (ESP-IDF). See ledc_driver.h
#include "driver/ledc.h"

#include "lumen/hardware/drivers/ledc_driver.h"

#include <cstdint>

namespace lumen
{

namespace
{

// Track which LEDC channels/timers are allocated to prevent conflicts.
// ESP-IDF LEDC is a global resource; this keeps driver instances from
// clobbering each other.
uint32_t g_channel_mask = 0; // bit i = channel i allocated
uint32_t g_timer_mask = 0;   // bit i = timer i allocated

bool allocate_channel(uint8_t channel, uint8_t timer)
{
    if (channel >= 16 || timer >= 8) {
        return false;
    }
    if ((g_channel_mask >> channel) & 1U) {
        return false;
    }
    if ((g_timer_mask >> timer) & 1U) {
        return false;
    }
    g_channel_mask |= (1U << channel);
    g_timer_mask |= (1U << timer);
    return true;
}

void release_channel(uint8_t channel, uint8_t timer)
{
    g_channel_mask &= ~(1U << channel);
    g_timer_mask &= ~(1U << timer);
}

} // namespace

LedcDriver::LedcDriver(uint8_t channel, uint8_t timer_num,
                       uint32_t frequency_hz, uint8_t resolution_bits)
    : channel_(channel),
      timer_num_(timer_num),
      frequency_hz_(frequency_hz),
      resolution_bits_(resolution_bits)
{
}

esp_err_t LedcDriver::initialize()
{
    if (state_ != DriverState::kUninitialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!allocate_channel(channel_, timer_num_)) {
        return ESP_ERR_NO_MEM; // channel/timer already in use
    }

    // Configure the LEDC timer.
    ledc_timer_config_t timer_conf{};
    timer_conf.speed_mode = LEDC_LOW_SPEED_MODE;
    timer_conf.timer_num = static_cast<ledc_timer_t>(timer_num_);
    timer_conf.duty_resolution = static_cast<ledc_timer_bit_t>(resolution_bits_);
    timer_conf.freq_hz = frequency_hz_;
    timer_conf.clk_cfg = LEDC_AUTO_CLK;

    esp_err_t err = ledc_timer_config(&timer_conf);
    if (err != ESP_OK) {
        release_channel(channel_, timer_num_);
        state_ = DriverState::kFailed;
        return err;
    }

    // Configure the LEDC channel bound to this GPIO/timer.
    ledc_channel_config_t ch_conf{};
    ch_conf.speed_mode = LEDC_LOW_SPEED_MODE;
    ch_conf.channel = static_cast<ledc_channel_t>(channel_);
    ch_conf.timer_sel = static_cast<ledc_timer_t>(timer_num_);
    ch_conf.intr_type = LEDC_INTR_DISABLE;
    ch_conf.gpio_num = static_cast<int>(channel_);
    ch_conf.duty = 0;
    ch_conf.hpoint = 0;

    err = ledc_channel_config(&ch_conf);
    if (err != ESP_OK) {
        release_channel(channel_, timer_num_);
        state_ = DriverState::kFailed;
        return err;
    }

    // Start with duty 0 (off).
    err = ledc_set_duty(LEDC_LOW_SPEED_MODE, static_cast<ledc_channel_t>(channel_), 0);
    if (err == ESP_OK) {
        err = ledc_update_duty(LEDC_LOW_SPEED_MODE, static_cast<ledc_channel_t>(channel_));
    }
    if (err != ESP_OK) {
        release_channel(channel_, timer_num_);
        state_ = DriverState::kFailed;
        return err;
    }

    state_ = DriverState::kInitialized;
    return ESP_OK;
}

esp_err_t LedcDriver::shutdown()
{
    esp_err_t err = ledc_set_duty(LEDC_LOW_SPEED_MODE,
                                  static_cast<ledc_channel_t>(channel_), 0);
    if (err == ESP_OK) {
        err = ledc_update_duty(LEDC_LOW_SPEED_MODE,
                               static_cast<ledc_channel_t>(channel_));
    }
    release_channel(channel_, timer_num_);
    state_ = DriverState::kUninitialized;
    return err;
}

DriverState LedcDriver::state() const
{
    return state_;
}

const char *LedcDriver::name() const
{
    return "ledc";
}

esp_err_t LedcDriver::set_duty(PwmDuty duty)
{
    if (state_ != DriverState::kInitialized) {
        return ESP_ERR_INVALID_STATE;
    }
    // Validate duty against resolution.
    PwmDuty max_duty = (PwmDuty(1) << resolution_bits_) - 1;
    if (duty > max_duty) {
        return ESP_ERR_INVALID_ARG;
    }
    uint32_t duty_raw = inverted_ ? (max_duty - duty) : duty;
    esp_err_t err = ledc_set_duty(LEDC_LOW_SPEED_MODE,
                                  static_cast<ledc_channel_t>(channel_),
                                  duty_raw);
    if (err == ESP_OK) {
        err = ledc_update_duty(LEDC_LOW_SPEED_MODE,
                               static_cast<ledc_channel_t>(channel_));
    }
    if (err == ESP_OK) {
        duty_ = duty;
    }
    return err;
}

esp_err_t LedcDriver::get_duty(PwmDuty &duty)
{
    duty = duty_;
    return ESP_OK;
}

esp_err_t LedcDriver::set_frequency(uint32_t frequency_hz)
{
    if (state_ != DriverState::kInitialized) {
        return ESP_ERR_INVALID_STATE;
    }
    // Changing timer freq affects all channels sharing this timer.
    esp_err_t err = ledc_set_freq(LEDC_LOW_SPEED_MODE,
                                  static_cast<ledc_timer_t>(timer_num_),
                                  frequency_hz);
    if (err == ESP_OK) {
        frequency_hz_ = frequency_hz;
    }
    return err;
}

esp_err_t LedcDriver::get_frequency(uint32_t &frequency_hz)
{
    frequency_hz = frequency_hz_;
    return ESP_OK;
}

esp_err_t LedcDriver::set_resolution(uint8_t resolution_bits)
{
    if (state_ != DriverState::kInitialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (resolution_bits < 1 || resolution_bits > 20) {
        return ESP_ERR_INVALID_ARG;
    }
    // Reconfigure timer with new resolution.
    ledc_timer_config_t timer_conf{};
    timer_conf.speed_mode = LEDC_LOW_SPEED_MODE;
    timer_conf.timer_num = static_cast<ledc_timer_t>(timer_num_);
    timer_conf.duty_resolution = static_cast<ledc_timer_bit_t>(resolution_bits_);
    timer_conf.freq_hz = frequency_hz_;
    timer_conf.clk_cfg = LEDC_AUTO_CLK;

    esp_err_t err = ledc_timer_config(&timer_conf);
    if (err == ESP_OK) {
        resolution_bits_ = resolution_bits;
        // Clamp duty to new range.
        PwmDuty max_duty = (PwmDuty(1) << resolution_bits_) - 1;
        if (duty_ > max_duty) {
            duty_ = max_duty;
        }
    }
    return err;
}

esp_err_t LedcDriver::get_resolution(uint8_t &resolution_bits)
{
    resolution_bits = resolution_bits_;
    return ESP_OK;
}

esp_err_t LedcDriver::set_inverted(bool inverted)
{
    inverted_ = inverted;
    // Re-apply current duty to reflect inversion.
    return set_duty(duty_);
}

esp_err_t LedcDriver::get_inverted(bool &inverted)
{
    inverted = inverted_;
    return ESP_OK;
}

} // namespace lumen
