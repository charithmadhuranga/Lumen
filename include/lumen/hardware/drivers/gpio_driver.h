// Lumen — GPIO driver (SPEC §7.1, P0).
//
// ESP-IDF GPIO implementation of IOutputDriver / IDigitalInputDriver.
// Direction, inversion and safe state are configured from the board profile.
#pragma once

#include "esp_err.h"

#include <string>

#include "lumen/hardware/hal/i_digital_input_driver.h"
#include "lumen/hardware/hal/i_output_driver.h"

#include <cstdint>

namespace lumen
{

// A single GPIO pin configured as input or output.
class GpioDriver final : public IOutputDriver, public IDigitalInputDriver
{
  public:
    // Construct for a pin. `direction` is "input" or "output".
    GpioDriver(int32_t gpio, const std::string &direction,
               bool inverted = false);

    // IDriver
    esp_err_t initialize() override;
    esp_err_t shutdown() override;
    DriverState state() const override;
    const char *name() const override;
    DriverType driver_type() const override { return DriverType::kGpio; }

    // IOutputDriver
    esp_err_t set_value(uint32_t value) override;
    esp_err_t get_value(uint32_t &value) override;
    esp_err_t set_safe_state(uint32_t value, bool active) override;
    esp_err_t get_safe_state(uint32_t &value, bool &active) override;

    // IDigitalInputDriver
    esp_err_t read(bool &state) override;
    esp_err_t set_inverted(bool inverted) override;
    esp_err_t get_inverted(bool &inverted) override;

  private:
    int32_t gpio_;
    std::string direction_;
    bool inverted_ = false;
    bool safe_active_ = false;
    uint32_t safe_value_ = 0;
    uint32_t value_ = 0;
    DriverState state_ = DriverState::kUninitialized;
};

} // namespace lumen
