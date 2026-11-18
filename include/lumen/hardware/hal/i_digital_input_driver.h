// Lumen HAL — digital input driver (SPEC §5.1).
//
// Range validation happens inside the driver, never the caller.
#pragma once

#include "i_driver.h"

namespace lumen
{

// A digital input. Reads the current level; debouncing is handled by the
// input_manager service (Phase 2.3), not here.
class IDigitalInputDriver : public IDriver
{
  public:
    // Read the current input level into `state`.
    virtual esp_err_t read(bool &state) = 0;

    // Configure input polarity (active-low inputs report inverted).
    virtual esp_err_t set_inverted(bool inverted) = 0;

    virtual esp_err_t get_inverted(bool &inverted) = 0;
};

} // namespace lumen
