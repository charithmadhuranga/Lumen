// Lumen system manager — see include/lumen/core/system_manager.hpp
#include <cstring>

#include "lumen/core/system_manager.hpp"

namespace lumen
{

SystemManager::SystemManager() = default;

bool SystemManager::register_service(IService &service)
{
    if (service_count_ >= kMaxServices) {
        return false;
    }
    // No duplicate names.
    for (std::size_t i = 0; i < service_count_; ++i) {
        if (services_[i].service != nullptr &&
            std::strcmp(services_[i].service->name(), service.name()) == 0) {
            return false;
        }
    }
    services_[service_count_].service = &service;
    services_[service_count_].registered = true;
    service_count_++;
    return true;
}

std::size_t SystemManager::find_index(const char *name) const
{
    if (name == nullptr) {
        return service_count_; // sentinel = not found
    }
    for (std::size_t i = 0; i < service_count_; ++i) {
        if (services_[i].service != nullptr &&
            std::strcmp(services_[i].service->name(), name) == 0) {
            return i;
        }
    }
    return service_count_; // not found
}

const IService *SystemManager::find_service(const char *name) const
{
    std::size_t idx = find_index(name);
    if (idx >= service_count_) {
        return nullptr;
    }
    return services_[idx].service;
}

ServiceResult SystemManager::init_all()
{
    any_failed_ = false;
    ServiceResult overall = ServiceResult::kOk;

    for (std::size_t i = 0; i < service_count_; ++i) {
        IService *svc = services_[i].service;
        if (svc == nullptr) {
            continue;
        }
        if (svc->status() != ServiceStatus::kRegistered) {
            // Already initialized (or started) — skip re-init.
            continue;
        }
        ServiceResult r = svc->init();
        if (r != ServiceResult::kOk) {
            any_failed_ = true;
            overall = ServiceResult::kFailed;
            // Bounded startup: continue initializing the rest.
        }
    }
    return overall;
}

ServiceResult SystemManager::start_all()
{
    ServiceResult overall = ServiceResult::kOk;

    for (std::size_t i = 0; i < service_count_; ++i) {
        IService *svc = services_[i].service;
        if (svc == nullptr) {
            continue;
        }
        if (svc->status() != ServiceStatus::kInitialized) {
            // Not initialized, or already started/stopped — skip.
            continue;
        }
        ServiceResult r = svc->start();
        if (r != ServiceResult::kOk) {
            overall = ServiceResult::kFailed;
        }
    }
    return overall;
}

ServiceResult SystemManager::stop_all()
{
    ServiceResult overall = ServiceResult::kOk;

    // Stop in reverse registration order.
    for (std::size_t i = service_count_; i > 0; --i) {
        IService *svc = services_[i - 1].service;
        if (svc == nullptr) {
            continue;
        }
        if (svc->status() != ServiceStatus::kStarted) {
            continue;
        }
        ServiceResult r = svc->stop();
        if (r != ServiceResult::kOk) {
            overall = ServiceResult::kFailed;
        }
    }
    return overall;
}

} // namespace lumen
