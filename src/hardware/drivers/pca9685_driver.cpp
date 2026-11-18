// Lumen — PCA9685 driver implementation (ESP-IDF). See pca9685_driver.h
#include "lumen/hardware/drivers/i2c_manager.h"
#include "lumen/hardware/drivers/pca9685_driver.h"

namespace lumen
{

namespace
{

// PCA9685 register map.
constexpr uint8_t kRegMode1 = 0x00;
constexpr uint8_t kRegMode2 = 0x01;
constexpr uint8_t kRegLed0OnL = 0x06;
constexpr uint8_t kRegAllLedOnL = 0xFA;
constexpr uint8_t kRegAllLedOffL = 0xFC;
constexpr uint8_t kRegPrescale = 0xFE;

// Mode1 bits.
constexpr uint8_t kMode1Restart = 0x80;
constexpr uint8_t kMode1Sleep = 0x10;
constexpr uint8_t kMode1AutoInc = 0x20;

// Mode2 bits.
constexpr uint8_t kMode2Inverted = 0x40;
constexpr uint8_t kMode2OutTotemPole = 0x04;

// Compute PCA9685 prescale for a target frequency.
// prescale = round(25000000 / (4096 * freq)) - 1 ; clamp 0x03..0xFF.
uint8_t compute_prescale(uint32_t frequency_hz)
{
    if (frequency_hz == 0) {
        frequency_hz = 1;
    }
    // 25 MHz internal oscillator.
    double prescale_double = 25000000.0 / (4096.0 * static_cast<double>(frequency_hz)) - 1.0;
    int prescale = static_cast<int>(prescale_double + 0.5);
    if (prescale < 3) {
        prescale = 3;
    }
    if (prescale > 255) {
        prescale = 255;
    }
    return static_cast<uint8_t>(prescale);
}

} // namespace

Pca9685Driver::Pca9685Driver(II2CBus &bus, uint8_t device_addr,
                             uint32_t frequency_hz)
    : bus_(bus), device_addr_(device_addr), frequency_hz_(frequency_hz)
{
    channel_duty_.fill(0);
    channel_on_.fill(false);
    channel_dirty_.fill(false);
}

esp_err_t Pca9685Driver::initialize()
{
    if (state_ != DriverState::kUninitialized) {
        return ESP_ERR_INVALID_STATE;
    }

    // Wake device: clear sleep bit.
    esp_err_t err = write_reg(kRegMode1, 0x00); // normal mode, no auto-inc yet
    if (err != ESP_OK) {
        state_ = DriverState::kFailed;
        return err;
    }

    // Mode2: totem-pole outputs, set inversion.
    uint8_t mode2 = kMode2OutTotemPole;
    if (inverted_) {
        mode2 |= kMode2Inverted;
    }
    err = write_reg(kRegMode2, mode2);
    if (err != ESP_OK) {
        state_ = DriverState::kFailed;
        return err;
    }

    // Set frequency (prescale).
    err = set_prescale(frequency_hz_);
    if (err != ESP_OK) {
        state_ = DriverState::kFailed;
        return err;
    }

    // Initialize all channels to off (safe state).
    for (uint8_t ch = 0; ch < kChannelCount; ++ch) {
        channel_duty_[ch] = 0;
        channel_on_[ch] = false;
        channel_dirty_[ch] = true;
    }

    // Mark initialized before flushing (flush checks state_).
    state_ = DriverState::kInitialized;
    err = flush();
    if (err != ESP_OK) {
        state_ = DriverState::kFailed;
        return err;
    }

    return ESP_OK;
}

esp_err_t Pca9685Driver::shutdown()
{
    // All channels off (safe state).
    for (uint8_t ch = 0; ch < kChannelCount; ++ch) {
        channel_duty_[ch] = 0;
        channel_on_[ch] = false;
        channel_dirty_[ch] = true;
    }
    esp_err_t err = flush();
    // Enter sleep.
    write_reg(kRegMode1, kMode1Sleep); // ignore error on shutdown path
    state_ = DriverState::kUninitialized;
    return err;
}

DriverState Pca9685Driver::state() const
{
    return state_;
}

const char *Pca9685Driver::name() const
{
    return "pca9685";
}

esp_err_t Pca9685Driver::write_reg(uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = {reg, val};
    return bus_.write(device_addr_, buf, 2);
}

esp_err_t Pca9685Driver::read_reg(uint8_t reg, uint8_t &val)
{
    return bus_.write_then_read(device_addr_, &reg, 1, &val, 1);
}

esp_err_t Pca9685Driver::set_prescale(uint32_t frequency_hz)
{
    // Must be in sleep mode to write prescale.
    esp_err_t err = write_reg(kRegMode1, kMode1Sleep);
    if (err != ESP_OK) {
        return err;
    }
    uint8_t prescale = compute_prescale(frequency_hz);
    err = write_reg(kRegPrescale, prescale);
    if (err != ESP_OK) {
        return err;
    }
    // Re-enable (clear sleep), set restart.
    err = write_reg(kRegMode1, kMode1Restart | kMode1AutoInc);
    if (err != ESP_OK) {
        return err;
    }
    frequency_hz_ = frequency_hz;
    return ESP_OK;
}

esp_err_t Pca9685Driver::set_duty(PwmDuty duty)
{
    return set_channel_duty(0, duty);
}

esp_err_t Pca9685Driver::get_duty(PwmDuty &duty)
{
    if (state_ != DriverState::kInitialized) {
        return ESP_ERR_INVALID_STATE;
    }
    duty = channel_duty_[0];
    return ESP_OK;
}

esp_err_t Pca9685Driver::set_frequency(uint32_t frequency_hz)
{
    // Validate frequency range (PCA9685: ~24Hz to ~1526Hz).
    if (frequency_hz < 24 || frequency_hz > 1526) {
        return ESP_ERR_INVALID_ARG;
    }
    return set_prescale(frequency_hz);
}

esp_err_t Pca9685Driver::get_frequency(uint32_t &frequency_hz)
{
    frequency_hz = frequency_hz_;
    return ESP_OK;
}

esp_err_t Pca9685Driver::set_resolution(uint8_t resolution_bits)
{
    // PCA9685 is fixed 12-bit.
    if (resolution_bits != 12) {
        return ESP_ERR_INVALID_ARG;
    }
    resolution_bits_ = resolution_bits;
    return ESP_OK;
}

esp_err_t Pca9685Driver::get_resolution(uint8_t &resolution_bits)
{
    resolution_bits = resolution_bits_;
    return ESP_OK;
}

esp_err_t Pca9685Driver::set_inverted(bool inverted)
{
    if (inverted == inverted_) {
        return ESP_OK;
    }
    uint8_t mode2 = kMode2OutTotemPole;
    if (inverted) {
        mode2 |= kMode2Inverted;
    }
    esp_err_t err = write_reg(kRegMode2, mode2);
    if (err != ESP_OK) {
        return err;
    }
    inverted_ = inverted;
    return ESP_OK;
}

esp_err_t Pca9685Driver::get_inverted(bool &inverted)
{
    inverted = inverted_;
    return ESP_OK;
}

esp_err_t Pca9685Driver::set_channel_duty(uint8_t channel, PwmDuty duty)
{
    if (channel >= kChannelCount) {
        return ESP_ERR_INVALID_ARG;
    }
    if (duty > kMaxDuty) {
        return ESP_ERR_INVALID_ARG;
    }
    channel_duty_[channel] = duty;
    channel_dirty_[channel] = true;
    return ESP_OK;
}

esp_err_t Pca9685Driver::set_channel_on(uint8_t channel, bool on)
{
    if (channel >= kChannelCount) {
        return ESP_ERR_INVALID_ARG;
    }
    channel_on_[channel] = on;
    channel_dirty_[channel] = true;
    return ESP_OK;
}

esp_err_t Pca9685Driver::get_channel_duty(uint8_t channel, PwmDuty &duty)
{
    if (channel >= kChannelCount) {
        return ESP_ERR_INVALID_ARG;
    }
    duty = channel_duty_[channel];
    return ESP_OK;
}

esp_err_t Pca9685Driver::queue_update(uint8_t channel, PwmDuty duty, bool on)
{
    if (channel >= kChannelCount || duty > kMaxDuty) {
        return ESP_ERR_INVALID_ARG;
    }
    channel_duty_[channel] = duty;
    channel_on_[channel] = on;
    channel_dirty_[channel] = true;
    return ESP_OK;
}

std::size_t Pca9685Driver::pending_count() const
{
    std::size_t n = 0;
    for (bool d : channel_dirty_) {
        if (d) {
            ++n;
        }
    }
    return n;
}

esp_err_t Pca9685Driver::flush()
{
    if (state_ != DriverState::kInitialized) {
        return ESP_ERR_INVALID_STATE;
    }

    // Collect dirty channels in ascending order for auto-increment batching.
    std::vector<uint8_t> dirty;
    dirty.reserve(kChannelCount);
    for (uint8_t ch = 0; ch < kChannelCount; ++ch) {
        if (channel_dirty_[ch]) {
            dirty.push_back(ch);
        }
    }

    if (dirty.empty()) {
        return ESP_OK;
    }

    esp_err_t last_err = ESP_OK;
    // Write each dirty channel's 4 registers. Use auto-increment from
    // LED0_ON_L of the first dirty channel; but channels aren't contiguous
    // necessarily. Simplest correct approach: one write per dirty channel
    // (4 bytes each). To batch contiguous runs, group them.
    std::size_t i = 0;
    while (i < dirty.size()) {
        // Find a contiguous run starting at dirty[i].
        std::size_t run_start = i;
        uint8_t first_ch = dirty[i];
        ++i;
        while (i < dirty.size() && dirty[i] == static_cast<uint8_t>(dirty[i - 1] + 1)) {
            ++i;
        }
        std::size_t run_len = i - run_start;

        // Build register block: for each channel in run, ON_L,ON_H,OFF_L,OFF_H.
        // We use auto-increment starting at LED0_ON_L of first_ch.
        uint8_t start_reg = kRegLed0OnL + (first_ch * 4);
        std::vector<uint8_t> buf;
        buf.reserve(run_len * 4 + 1); // +1 for register byte
        buf.push_back(start_reg);
        for (std::size_t r = 0; r < run_len; ++r) {
            uint8_t ch = dirty[run_start + r];
            uint16_t on_val = 0;
            uint16_t off_val = channel_on_[ch] ? channel_duty_[ch] : 0;
            // If off_val == 0 and on wanted, treat as full-on: ON=0, OFF=4096.
            // PCA9685: OFF=0 with ON=0 means full-on? Actually duty 0 = off.
            // For full-on we set ON_L=0x00, ON_H=0x10 (bit4=full), OFF_L/H=0.
            uint8_t on_h = 0;
            uint8_t off_h = 0;
            if (channel_on_[ch] && channel_duty_[ch] == kMaxDuty) {
                // Full on.
                on_h = 0x10; // bit4 = full on
            }
            buf.push_back(static_cast<uint8_t>(on_val & 0xFF));
            buf.push_back(on_h);
            buf.push_back(static_cast<uint8_t>(off_val & 0xFF));
            buf.push_back(off_h);
        }
        last_err = bus_.write(device_addr_, buf.data(), buf.size());
        if (last_err != ESP_OK) {
            break;
        }
    }

    // Mark all dirty as clean only on full success.
    if (last_err == ESP_OK) {
        for (uint8_t ch : dirty) {
            channel_dirty_[ch] = false;
        }
    }
    return last_err;
}

} // namespace lumen
