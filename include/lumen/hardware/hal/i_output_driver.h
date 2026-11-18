// Lumen HAL — digital output driver (SPEC §5.1).
//
// Range validation happens inside the driver, never the caller.
#pragma once

#include "i_driver.h"

namespace lumen
{

// A digital output: on/off with optional inversion and a configured safe
// state applied on shutdown / profile apply rollback.
class IOutputDriver : public IDriver
{
  public:
    // Set the output value. `value` is interpreted per-driver (0/1 or
    // 0/255); the driver validates the range.
    virtual esp_err_t set_value(uint32_t value) = 0;

    // Read the last applied value back.
    virtual esp_err_t get_value(uint32_t &value) = 0;

    // Configure the safe state applied when the driver is stopped or a
    // profile apply is rolled back.
    virtual esp_err_t set_safe_state(uint32_t value, bool active) = 0;

    virtual esp_err_t get_safe_state(uint32_t &value, bool &active) = 0;
};

} // namespace lumen
