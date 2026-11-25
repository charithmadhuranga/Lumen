// Lumen — LEDC PWM driver implementation (ESP-IDF). See ledc_driver.h
#include "lumen/hardware/drivers/ledc_driver.h"

#include "driver/ledc.h"
#include "esp_log.h"

#include <mutex>

namespace lumen
{

namespace
{
const char *TAG = "ledc";

constexpr int kMaxChannels = 8; // low-speed channels (ESP32/S3)
constexpr int kMaxTimers = 4;

struct TimerSlot
{
    uint32_t frequency_hz = 0;
    uint8_t resolution_bits = 0;
    int users = 0;
};

std::mutex g_mutex;
bool g_channel_used[kMaxChannels] = {};
TimerSlot g_timers[kMaxTimers] = {};

int allocate_channel()
{
    for (int i = 0; i < kMaxChannels; ++i) {
        if (!g_channel_used[i]) {
            g_channel_used[i] = true;
            return i;
        }
    }
    return -1;
}

// Reuse a timer already running at the same frequency/resolution, else
// claim a free one. Returns -1 when all timers are taken.
int acquire_timer(uint32_t frequency_hz, uint8_t resolution_bits, bool &is_new)
{
    for (int i = 0; i < kMaxTimers; ++i) {
        if (g_timers[i].users > 0 && g_timers[i].frequency_hz == frequency_hz &&
            g_timers[i].resolution_bits == resolution_bits) {
            g_timers[i].users++;
            is_new = false;
            return i;
        }
    }
    for (int i = 0; i < kMaxTimers; ++i) {
        if (g_timers[i].users == 0) {
            g_timers[i] = {frequency_hz, resolution_bits, 1};
            is_new = true;
            return i;
        }
    }
    return -1;
}

void release(int channel, int timer)
{
    if (channel >= 0 && channel < kMaxChannels) {
        g_channel_used[channel] = false;
    }
    if (timer >= 0 && timer < kMaxTimers && g_timers[timer].users > 0) {
        g_timers[timer].users--;
    }
}
} // namespace

LedcDriver::LedcDriver(int32_t gpio, uint32_t frequency_hz,
                       uint8_t resolution_bits, bool inverted)
    : gpio_(gpio), frequency_hz_(frequency_hz),
      resolution_bits_(resolution_bits), inverted_(inverted)
{
}

LedcDriver::~LedcDriver()
{
    shutdown();
}

esp_err_t LedcDriver::initialize()
{
    if (state_ == DriverState::kInitialized) {
        return ESP_OK;
    }
    if (gpio_ < 0 || resolution_bits_ < 1 || resolution_bits_ > 16) {
        state_ = DriverState::kFailed;
        return ESP_ERR_INVALID_ARG;
    }
    std::lock_guard<std::mutex> lock(g_mutex);
    channel_ = allocate_channel();
    bool new_timer = false;
    timer_ = acquire_timer(frequency_hz_, resolution_bits_, new_timer);
    if (channel_ < 0 || timer_ < 0) {
        release(channel_, timer_);
        channel_ = timer_ = -1;
        state_ = DriverState::kFailed;
        return ESP_ERR_NO_MEM;
    }
    esp_err_t err = ESP_OK;
    if (new_timer) {
        ledc_timer_config_t timer_conf{};
        timer_conf.speed_mode = LEDC_LOW_SPEED_MODE;
        timer_conf.timer_num = static_cast<ledc_timer_t>(timer_);
        timer_conf.duty_resolution = static_cast<ledc_timer_bit_t>(resolution_bits_);
        timer_conf.freq_hz = frequency_hz_;
        timer_conf.clk_cfg = LEDC_AUTO_CLK;
        err = ledc_timer_config(&timer_conf);
    }
    if (err == ESP_OK) {
        ledc_channel_config_t ch{};
        ch.speed_mode = LEDC_LOW_SPEED_MODE;
        ch.channel = static_cast<ledc_channel_t>(channel_);
        ch.timer_sel = static_cast<ledc_timer_t>(timer_);
        ch.intr_type = LEDC_INTR_DISABLE;
        ch.gpio_num = static_cast<int>(gpio_);
        ch.duty = inverted_ ? ((1U << resolution_bits_) - 1U) : 0; // safe: off
        ch.hpoint = 0;
        err = ledc_channel_config(&ch);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "gpio %ld init failed: %s", static_cast<long>(gpio_),
                 esp_err_to_name(err));
        release(channel_, timer_);
        channel_ = timer_ = -1;
        state_ = DriverState::kFailed;
        return err;
    }
    duty_ = 0;
    state_ = DriverState::kInitialized;
    return ESP_OK;
}

esp_err_t LedcDriver::shutdown()
{
    if (state_ != DriverState::kInitialized) {
        state_ = DriverState::kUninitialized;
        return ESP_OK;
    }
    esp_err_t err = write_raw(0); // safe state: off
    ledc_stop(LEDC_LOW_SPEED_MODE, static_cast<ledc_channel_t>(channel_),
              inverted_ ? 1 : 0);
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        release(channel_, timer_);
    }
    channel_ = timer_ = -1;
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

esp_err_t LedcDriver::write_raw(PwmDuty duty)
{
    const uint32_t max_raw = (1U << resolution_bits_) - 1U;
    uint32_t raw = (static_cast<uint32_t>(duty) * max_raw + 32767U) / 65535U;
    if (inverted_) {
        raw = max_raw - raw;
    }
    esp_err_t err = ledc_set_duty(LEDC_LOW_SPEED_MODE,
                                  static_cast<ledc_channel_t>(channel_), raw);
    if (err == ESP_OK) {
        err = ledc_update_duty(LEDC_LOW_SPEED_MODE,
                               static_cast<ledc_channel_t>(channel_));
    }
    return err;
}

esp_err_t LedcDriver::set_duty(PwmDuty duty)
{
    if (state_ != DriverState::kInitialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (duty == duty_) {
        return ESP_OK; // redundant write skipped
    }
    esp_err_t err = write_raw(duty);
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

esp_err_t LedcDriver::set_channel_duty(uint8_t channel, PwmDuty duty)
{
    if (channel != 0) {
        return ESP_ERR_INVALID_ARG;
    }
    return set_duty(duty);
}

esp_err_t LedcDriver::set_frequency(uint32_t frequency_hz)
{
    if (state_ != DriverState::kInitialized) {
        frequency_hz_ = frequency_hz;
        return ESP_OK;
    }
    // Timers may be shared; changing frequency on a live shared timer would
    // retime other channels, so it is only allowed before initialize().
    return frequency_hz == frequency_hz_ ? ESP_OK : ESP_ERR_INVALID_STATE;
}

esp_err_t LedcDriver::get_frequency(uint32_t &frequency_hz)
{
    frequency_hz = frequency_hz_;
    return ESP_OK;
}

esp_err_t LedcDriver::set_resolution(uint8_t resolution_bits)
{
    if (resolution_bits < 1 || resolution_bits > 16) {
        return ESP_ERR_INVALID_ARG;
    }
    if (state_ == DriverState::kInitialized) {
        return resolution_bits == resolution_bits_ ? ESP_OK : ESP_ERR_INVALID_STATE;
    }
    resolution_bits_ = resolution_bits;
    return ESP_OK;
}

esp_err_t LedcDriver::get_resolution(uint8_t &resolution_bits)
{
    resolution_bits = resolution_bits_;
    return ESP_OK;
}

esp_err_t LedcDriver::set_inverted(bool inverted)
{
    inverted_ = inverted;
    if (state_ != DriverState::kInitialized) {
        return ESP_OK;
    }
    return write_raw(duty_);
}

esp_err_t LedcDriver::get_inverted(bool &inverted)
{
    inverted = inverted_;
    return ESP_OK;
}

} // namespace lumen
