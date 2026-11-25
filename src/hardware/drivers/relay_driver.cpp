// Lumen — relay driver implementation (ESP-IDF). See relay_driver.h
#include "driver/gpio.h"

#include "lumen/hardware/drivers/relay_driver.h"

namespace lumen
{

RelayDriver::RelayDriver(int32_t gpio, RelayPolarity polarity)
    : gpio_(gpio), polarity_(polarity)
{
}

esp_err_t RelayDriver::initialize()
{
    gpio_config_t io_conf{};
    io_conf.pin_bit_mask = 1ULL << gpio_;
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    // Latch the safe (off) level before enabling the output driver.
    gpio_set_level(static_cast<gpio_num_t>(gpio_),
                   ((polarity_ == RelayPolarity::kActiveHigh) ? safe_on_ : !safe_on_) ? 1 : 0);
    esp_err_t err = gpio_config(&io_conf);
    if (err != ESP_OK) {
        state_ = DriverState::kFailed;
        return err;
    }

    // Apply safe state (off unless safe_state says on).
    bool safe_on = (polarity_ == RelayPolarity::kActiveHigh) ? safe_on_ : !safe_on_;
    err = gpio_set_level(static_cast<gpio_num_t>(gpio_), safe_on ? 1 : 0);
    if (err != ESP_OK) {
        state_ = DriverState::kFailed;
        return err;
    }
    state_ = DriverState::kInitialized;
    return ESP_OK;
}

esp_err_t RelayDriver::shutdown()
{
    bool safe_on = (polarity_ == RelayPolarity::kActiveHigh) ? safe_on_ : !safe_on_;
    esp_err_t err = gpio_set_level(static_cast<gpio_num_t>(gpio_), safe_on ? 1 : 0);
    if (err != ESP_OK) {
        return err;
    }
    state_ = DriverState::kUninitialized;
    return ESP_OK;
}

DriverState RelayDriver::state() const
{
    return state_;
}

const char *RelayDriver::name() const
{
    return "relay";
}

esp_err_t RelayDriver::set_value_impl(bool on)
{
    // active-high: level = on ? 1 : 0 ; active-low: level = on ? 0 : 1
    int level;
    if (polarity_ == RelayPolarity::kActiveHigh) {
        level = on ? 1 : 0;
    } else {
        level = on ? 0 : 1;
    }
    esp_err_t err = gpio_set_level(static_cast<gpio_num_t>(gpio_), level);
    if (err == ESP_OK) {
        on_ = on;
    }
    return err;
}

esp_err_t RelayDriver::set_value(uint32_t value)
{
    return set_value_impl(value != 0);
}

esp_err_t RelayDriver::get_value(uint32_t &value)
{
    value = on_ ? 1 : 0;
    return ESP_OK;
}

esp_err_t RelayDriver::set_safe_state(uint32_t value, bool active)
{
    safe_on_ = (value != 0);
    safe_active_ = active;
    return ESP_OK;
}

esp_err_t RelayDriver::get_safe_state(uint32_t &value, bool &active)
{
    value = safe_on_ ? 1 : 0;
    active = safe_active_;
    return ESP_OK;
}

esp_err_t RelayDriver::set_polarity(RelayPolarity polarity)
{
    polarity_ = polarity;
    return ESP_OK;
}

esp_err_t RelayDriver::get_polarity(RelayPolarity &polarity)
{
    polarity = polarity_;
    return ESP_OK;
}

esp_err_t RelayDriver::turn_on()
{
    return set_value_impl(true);
}

esp_err_t RelayDriver::turn_off()
{
    return set_value_impl(false);
}

} // namespace lumen
