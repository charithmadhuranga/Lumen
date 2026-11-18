// Lumen diagnostics — see include/lumen/core/diagnostics.hpp
#include <algorithm>
#include <cstring>

#include "lumen/core/diagnostics.hpp"

namespace lumen
{

namespace
{

const char *kResetReasonUnknown = "unknown";

} // namespace

const char *category_name(LogCategory category)
{
    switch (category) {
    case LogCategory::kSystem:
        return "SYSTEM";
    case LogCategory::kHardware:
        return "HARDWARE";
    case LogCategory::kLighting:
        return "LIGHTING";
    case LogCategory::kAutomation:
        return "AUTOMATION";
    case LogCategory::kNetwork:
        return "NETWORK";
    case LogCategory::kMqtt:
        return "MQTT";
    case LogCategory::kEspNow:
        return "ESP_NOW";
    case LogCategory::kWeb:
        return "WEB";
    case LogCategory::kOta:
        return "OTA";
    case LogCategory::kSecurity:
        return "SECURITY";
    }
    return "UNKNOWN";
}

std::string redact_secret(const std::string &value)
{
    if (value.empty()) {
        return "";
    }
    return "[REDACTED]";
}

std::string mask_secret(const std::string &value, char mask_char,
                        std::size_t keep)
{
    if (value.empty()) {
        return "";
    }
    if (value.size() <= keep) {
        return std::string(value.size(), mask_char);
    }
    std::string masked(value.size() - keep, mask_char);
    masked += value.substr(value.size() - keep);
    return masked;
}

Snapshot take_snapshot(uint32_t uptime_ms, uint32_t free_heap_bytes,
                       uint32_t min_free_heap_bytes, const char *reset_reason,
                       bool any_service_failed)
{
    Snapshot snap;
    snap.uptime_ms = uptime_ms;
    snap.free_heap_bytes = free_heap_bytes;
    snap.min_free_heap_bytes = min_free_heap_bytes;
    snap.reset_reason = (reset_reason != nullptr) ? reset_reason
                                                  : kResetReasonUnknown;
    snap.any_service_failed = any_service_failed;
    return snap;
}

} // namespace lumen
