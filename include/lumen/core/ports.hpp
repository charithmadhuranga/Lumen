// Lumen core ports — tiny platform interfaces so core logic stays free of
// ESP-IDF primitives and runs under `pio test -e native` (AGENTS.md §11).
//
// Target implementations live in src/core/platform/ (ESP32 only).
// Host tests use the no-op / steady-clock implementations inline in tests.
#pragma once

#include <cstdint>

namespace lumen
{

// Critical-section port. On target: ESP-IDF portMUX. On host: no-op
// (host tests are single-threaded by contract).
class ILock
{
  public:
    virtual ~ILock() = default;
    virtual void lock() = 0;
    virtual void unlock() = 0;
};

// Millisecond uptime clock. On target: esp_timer. On host: steady_clock.
class IClock
{
  public:
    virtual ~IClock() = default;
    virtual uint32_t ms() = 0;
};

} // namespace lumen
