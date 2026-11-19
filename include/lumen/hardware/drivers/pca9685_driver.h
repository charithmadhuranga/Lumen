// Lumen — PCA9685 16-channel PWM driver (SPEC §7.1, P0; §7.2).
//
// 16 channels, 12-bit resolution, configurable frequency/polarity, batched
// pending updates, I2C error recovery, safe-state init.
//
// Lighting channels never talk to I2C directly; they enqueue duty changes
// here and call flush() to batch writes (AGENTS.md §6).
#pragma once

#include "esp_err.h"

#include "lumen/hardware/hal/i_pwm_driver.h"

#include <array>
#include <cstdint>
#include <vector>

namespace lumen
{

// Forward: the I2C bus this driver uses.
class I2cManager;

// A pending channel update.
struct PcaChannelUpdate
{
    uint8_t channel = 0;   // 0..15
    uint16_t duty = 0;     // 0..4095
    bool on = false;       // output enable for this channel
};

// PCA9685 driver. Uses an II2CBus interface (not the concrete manager) so it
// is host-testable with a mock bus.
class II2CBus;

class Pca9685Driver final : public IPwmDriver
{
  public:
    static constexpr uint8_t kChannelCount = 16;
    static constexpr uint16_t kMaxDuty = 4095;

    Pca9685Driver(II2CBus &bus, uint8_t device_addr,
                  uint32_t frequency_hz = 1000);

    // IDriver
    esp_err_t initialize() override;
    esp_err_t shutdown() override;
    DriverState state() const override;
    const char *name() const override;
    DriverType driver_type() const override { return DriverType::kPwm; }

    // IPwmDriver (channel-agnostic default channel 0).
    esp_err_t set_duty(PwmDuty duty) override;
    esp_err_t get_duty(PwmDuty &duty) override;
    esp_err_t set_frequency(uint32_t frequency_hz) override;
    esp_err_t get_frequency(uint32_t &frequency_hz) override;
    esp_err_t set_resolution(uint8_t resolution_bits) override;
    esp_err_t get_resolution(uint8_t &resolution_bits) override;
    esp_err_t set_inverted(bool inverted) override;
    esp_err_t get_inverted(bool &inverted) override;

    // Per-channel control.
    esp_err_t set_channel_duty(uint8_t channel, PwmDuty duty);
    esp_err_t set_channel_on(uint8_t channel, bool on);
    esp_err_t get_channel_duty(uint8_t channel, PwmDuty &duty);

    // Queue a channel update for batched flush.
    esp_err_t queue_update(uint8_t channel, PwmDuty duty, bool on);

    // Flush all pending updates to the PCA9685 in as few I2C transactions as
    // possible. Returns ESP_OK if all writes succeeded.
    esp_err_t flush();

    // Number of pending (dirty) updates.
    std::size_t pending_count() const;

  private:
    II2CBus &bus_;          // bus (interface)
    uint8_t device_addr_;
    uint32_t frequency_hz_;
    uint8_t resolution_bits_ = 12;
    bool inverted_ = false;
    DriverState state_ = DriverState::kUninitialized;

    std::array<uint16_t, kChannelCount> channel_duty_{};
    std::array<bool, kChannelCount> channel_on_{};
    std::array<bool, kChannelCount> channel_dirty_{};

    esp_err_t write_reg(uint8_t reg, uint8_t val);
    esp_err_t read_reg(uint8_t reg, uint8_t &val);
    esp_err_t set_prescale(uint32_t frequency_hz);
};

} // namespace lumen
