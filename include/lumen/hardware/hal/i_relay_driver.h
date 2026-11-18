// Lumen HAL — relay driver (SPEC §5.1, §7.1).
//
// On/off with configured polarity (active-high / active-low) and safe state.
#pragma once

#include "i_output_driver.h"

namespace lumen
{

enum class RelayPolarity : uint8_t
{
    kActiveHigh = 0,
    kActiveLow = 1,
};

class IRelayDriver : public IOutputDriver
{
  public:
    virtual esp_err_t set_polarity(RelayPolarity polarity) = 0;
    virtual esp_err_t get_polarity(RelayPolarity &polarity) = 0;

    virtual esp_err_t turn_on() = 0;
    virtual esp_err_t turn_off() = 0;
};

} // namespace lumen
