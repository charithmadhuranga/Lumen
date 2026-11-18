// Lumen — relay driver (SPEC §7.1, P0).
//
// On/off with configured polarity (active-high / active-low) and safe state.
// Wraps a GPIO output.
#pragma once

#include "esp_err.h"

#include "lumen/hardware/hal/i_relay_driver.h"

#include <string>

namespace lumen
{

class RelayDriver final : public IRelayDriver
{
  public:
    RelayDriver(int32_t gpio, RelayPolarity polarity = RelayPolarity::kActiveHigh);

    // IDriver
    esp_err_t initialize() override;
    esp_err_t shutdown() override;
    DriverState state() const override;
    const char *name() const override;

    // IOutputDriver
    esp_err_t set_value(uint32_t value) override;
    esp_err_t get_value(uint32_t &value) override;
    esp_err_t set_safe_state(uint32_t value, bool active) override;
    esp_err_t get_safe_state(uint32_t &value, bool &active) override;

    // IRelayDriver
    esp_err_t set_polarity(RelayPolarity polarity) override;
    esp_err_t get_polarity(RelayPolarity &polarity) override;
    esp_err_t turn_on() override;
    esp_err_t turn_off() override;

  private:
    int32_t gpio_;
    RelayPolarity polarity_ = RelayPolarity::kActiveHigh;
    bool safe_active_ = false;
    bool safe_on_ = false;
    bool on_ = false;
    DriverState state_ = DriverState::kUninitialized;

    esp_err_t set_value_impl(bool on);
};

} // namespace lumen
