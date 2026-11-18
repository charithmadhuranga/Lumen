// Lumen service interface — include/lumen/core/service.hpp
//
// Every service managed by the SystemManager implements IService. Services
// are registered declaratively; the SystemManager drives init/start/stop in
// the declared order (auditable startup sequence, docs/ARCHITECTURE.md).
//
// No task is created implicitly by a service or by the SystemManager
// (AGENTS.md §5). On target, a service may create its own FreeRTOS task, but
// that task's priority/stack is declared in the registry table and
// documented in docs/ARCHITECTURE.md.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace lumen
{

// Service lifecycle status.
enum class ServiceStatus : uint8_t
{
    kRegistered = 0,  // registered, not yet initialized
    kInitialized,     // init() succeeded
    kStarted,         // start() succeeded
    kStopped,         // stop() called
    kFailed,          // init() or start() failed
};

// Result of init/start/stop.
enum class ServiceResult : uint8_t
{
    kOk = 0,
    kFailed,          // service-specific failure
    kInvalidState,    // called out of lifecycle order
};

// Safe output state a service can configure for shutdown (gate item 5).
struct SafeState
{
    bool active = false;   // whether the output is driven in safe state
    int32_t value = 0;     // safe value (e.g. brightness 0 / relay off)
};

class IService
{
  public:
    virtual ~IService() = default;

    // Human-readable service name (used in logs and the task registry).
    virtual const char *name() const = 0;

    // One-time initialization. May allocate bounded resources. Must not block
    // for extended periods. Returns kFailed if the service cannot initialize
    // (the SystemManager continues with other services — bounded startup).
    virtual ServiceResult init() = 0;

    // Start the service. Called after init(). May begin producing events.
    virtual ServiceResult start() = 0;

    // Stop the service. Must put any outputs into the configured safe state
    // and release resources. Called in reverse registration order.
    virtual ServiceResult stop() = 0;

    // Current lifecycle status.
    virtual ServiceStatus status() const = 0;

    // Configure the safe output state used by stop().
    virtual void set_safe_state(const SafeState &state) = 0;
    virtual SafeState safe_state() const = 0;
};

} // namespace lumen
