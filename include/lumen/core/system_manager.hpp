// Lumen system manager — include/lumen/core/system_manager.hpp
//
// Declarative service registry (SPEC §26 Phase 1, M4). The SystemManager owns
// the service lifecycle: init → start → stop, in declared order. One service
// failing to initialize does not prevent the rest of the system from booting
// (bounded startup, SPEC §1.2 rule 6).
//
// No FreeRTOS is used here; the manager is plain C++ and fully host-testable.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "lumen/core/service.hpp"

namespace lumen
{

class SystemManager
{
  public:
    static constexpr std::size_t kMaxServices = 16;

    SystemManager();

    // Register a service. Returns false when the registry is full or the
    // service is already registered.
    bool register_service(IService &service);

    // Initialize every registered service in order. A service that fails is
    // marked kFailed but initialization continues (bounded startup). Returns
    // kOk if all services initialized, kFailed if any failed.
    ServiceResult init_all();

    // Start every initialized service in order. Returns kOk if all started,
    // kFailed if any failed (already-started services are not re-started).
    ServiceResult start_all();

    // Stop every started service in reverse order, putting outputs into
    // their configured safe states.
    ServiceResult stop_all();

    // Status of a registered service (lookup by name). kNotFound equivalent
    // is ServiceStatus::kRegistered with name not found -> returns nullptr.
    const IService *find_service(const char *name) const;

    std::size_t service_count() const { return service_count_; }
    bool any_failed() const { return any_failed_; }

  private:
    struct Entry
    {
        IService *service = nullptr;
        bool registered = false;
    };

    Entry services_[kMaxServices]{};
    std::size_t service_count_ = 0;
    bool any_failed_ = false;

    std::size_t find_index(const char *name) const;
};

} // namespace lumen
