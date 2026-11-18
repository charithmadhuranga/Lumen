// Lumen HAL — generic peripheral driver (SPEC §5.1).
//
// Catch-all for devices not covered by the specific drivers (e.g. sensors,
// IO expanders beyond PCF8574). Implementers define their own init/ioctl.
#pragma once

#include "i_driver.h"

namespace lumen
{

class IPeripheralDriver : public IDriver
{
  public:
    // Device-specific ioctl; `cmd` and `arg` semantics are driver-defined.
    virtual esp_err_t ioctl(uint32_t cmd, void *arg) = 0;
};

} // namespace lumen
