// Lumen — GPIO driver implementation (ESP-IDF). See gpio_driver.h
#include "driver/gpio.h"

#include "lumen/hardware/drivers/gpio_driver.h"

namespace lumen
{

GpioDriver::GpioDriver(int32_t gpio, const std::string &direction,
                       bool inverted)
    : gpio_(gpio), direction_(direction), inverted_(inverted)
{
}

esp_err_t GpioDriver::initialize()
{
    gpio_config_t io_conf{};
    io_conf.pin_bit_mask = 1ULL << gpio_;
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = (direction_ == "output") ? GPIO_MODE_OUTPUT
                                            : GPIO_MODE_INPUT;
    io_conf.pull_up_en = (pull_ == "up") ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE;
    io_conf.pull_down_en =
        (pull_ == "down") ? GPIO_PULLDOWN_ENABLE : GPIO_PULLDOWN_DISABLE;

    // Outputs: latch the safe level BEFORE switching the pin to output so
    // there is no glitch at boot.
    if (direction_ == "output") {
        gpio_set_level(static_cast<gpio_num_t>(gpio_), safe_active_ ? safe_value_ : 0);
    }

    esp_err_t err = gpio_config(&io_conf);
    if (err != ESP_OK) {
        state_ = DriverState::kFailed;
        return err;
    }

    // Apply safe state immediately so outputs start in a known state.
    if (direction_ == "output") {
        err = gpio_set_level(static_cast<gpio_num_t>(gpio_),
                             safe_active_ ? safe_value_ : 0);
        if (err != ESP_OK) {
            state_ = DriverState::kFailed;
            return err;
        }
    }

    state_ = DriverState::kInitialized;
    return ESP_OK;
}

esp_err_t GpioDriver::shutdown()
{
    if (direction_ == "output") {
        // Drive safe state before releasing.
        esp_err_t err = gpio_set_level(static_cast<gpio_num_t>(gpio_),
                                       safe_active_ ? safe_value_ : 0);
        if (err != ESP_OK) {
            return err;
        }
    }
    state_ = DriverState::kUninitialized;
    return ESP_OK;
}

DriverState GpioDriver::state() const
{
    return state_;
}

const char *GpioDriver::name() const
{
    return "gpio";
}

esp_err_t GpioDriver::set_value(uint32_t value)
{
    if (direction_ != "output") {
        return ESP_ERR_INVALID_STATE;
    }
    // Validate: only 0/1 are meaningful for digital output.
    if (value > 1) {
        return ESP_ERR_INVALID_ARG;
    }
    uint32_t level = inverted_ ? (value == 0 ? 1 : 0) : value;
    esp_err_t err = gpio_set_level(static_cast<gpio_num_t>(gpio_),
                                   static_cast<int>(level));
    if (err == ESP_OK) {
        value_ = value;
    }
    return err;
}

esp_err_t GpioDriver::get_value(uint32_t &value)
{
    value = value_;
    return ESP_OK;
}

esp_err_t GpioDriver::set_safe_state(uint32_t value, bool active)
{
    safe_value_ = value;
    safe_active_ = active;
    return ESP_OK;
}

esp_err_t GpioDriver::get_safe_state(uint32_t &value, bool &active)
{
    value = safe_value_;
    active = safe_active_;
    return ESP_OK;
}

esp_err_t GpioDriver::read(bool &state)
{
    if (direction_ != "input") {
        return ESP_ERR_INVALID_STATE;
    }
    int level = gpio_get_level(static_cast<gpio_num_t>(gpio_));
    if (level < 0) {
        return ESP_FAIL;
    }
    state = inverted_ ? (level == 0) : (level != 0);
    return ESP_OK;
}

esp_err_t GpioDriver::set_inverted(bool inverted)
{
    inverted_ = inverted;
    return ESP_OK;
}

esp_err_t GpioDriver::get_inverted(bool &inverted)
{
    inverted = inverted_;
    return ESP_OK;
}

} // namespace lumen
