// Lumen HAL — base driver interface.
//
// All hardware drivers implement this base; nothing here knows about boards
// or pins (AGENTS.md §4, SPEC §5.1).
#pragma once

#include <esp_err.h>

#include <cstddef>

namespace lumen
{

// Lifecycle stage reported by a driver. Helps the hardware manager decide
// whether a driver can be safely stopped/replaced during a profile apply.
enum class DriverState : uint8_t
{
    kUninitialized = 0,
    kInitialized,
    kStarted,
    kFailed,
};

// Every HAL driver. init/shutdown return esp_err_t; all runtime methods
// return esp_err_t so failures are never silently ignored.
class IDriver
{
  public:
    virtual ~IDriver() = default;

    // Initialize the driver and claim its resources. Idempotent-safe.
    virtual esp_err_t initialize() = 0;

    // Shut down the driver and release its resources. Safe to call when
    // already shut down.
    virtual esp_err_t shutdown() = 0;

    virtual DriverState state() const = 0;

    // Human-readable driver name (for logs and diagnostics).
    virtual const char *name() const = 0;
};

} // namespace lumen
