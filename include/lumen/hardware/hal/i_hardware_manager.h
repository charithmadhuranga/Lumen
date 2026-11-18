// Lumen HAL — hardware manager (coordinator).
//
// Owns all drivers registered against a board profile. Coordinates the
// transactional profile-apply workflow (SPEC §5.4): validate candidate ->
// stop affected services -> safe states -> init new hardware -> diagnostics
// -> commit, or restore previous profile on failure.
//
// The manager itself is board-agnostic; it knows nothing about pins or
// specific peripherals — those live in the profile + PinValidator.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "hal/i_driver.h"

namespace lumen
{

struct DriverRef
{
    std::string id;          // e.g. "gpio_0", "ledc_0", "i2c_0"
    IDriver *driver = nullptr;
};

// Result of validating + applying a candidate profile.
enum class ApplyResult : uint8_t
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

    // Register a driver under `id`. Returns false if `id` already exists.
    virtual bool register_driver(const std::string &id, IDriver &driver) = 0;

    // Look up a registered driver. Returns nullptr when unknown.
    virtual IDriver *driver(const std::string &id) = 0;

    // Initialize all registered drivers. Returns ESP_OK when all succeed;
    // drivers that fail individually are marked kFailed but initialization
    // continues (bounded startup, SPEC §1.2 rule 6).
    virtual esp_err_t initialize_all() = 0;

    // Shut down all registered drivers.
    virtual esp_err_t shutdown_all() = 0;

    // Apply a candidate board profile transactionally (SPEC §5.4).
    // Returns an ApplyResult; on any failure the previous profile is
    // restored and config is left intact.
    virtual ApplyResult apply_profile(const std::string &candidate_json) = 0;
};

} // namespace lumen
