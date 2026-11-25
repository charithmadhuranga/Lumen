// Lumen HAL — hardware manager (coordinator) interface.
//
// The lighting engine's view of the hardware: resolve a driver by the
// board-profile id a channel references, and flush batched outputs once per
// lighting tick. Board-agnostic — it knows nothing about pins.
#pragma once

#include <string>

#include "lumen/hardware/hal/i_driver.h"

namespace lumen
{

// Result of validating + applying a candidate profile (SPEC §5.4).
enum class ProfileApplyResult : uint8_t
{
    kOk = 0,
    kValidationFailed,
    kInitFailed,
    kDiagnosticFailed,
    kRolledBack,
};

class IHardwareManager
{
  public:
    virtual ~IHardwareManager() = default;

    // Look up a driver by its board-profile id (peripheral id such as
    // "pca9685_0" / "relay1", or PWM resource id). nullptr when unknown or
    // the driver failed to initialise.
    virtual IDriver *driver(const std::string &id) = 0;

    // Flush every batched output device (PCA9685 pending channel writes).
    // Called once per lighting tick so N channel updates cost one I2C burst.
    virtual esp_err_t flush_outputs() = 0;
};

} // namespace lumen
