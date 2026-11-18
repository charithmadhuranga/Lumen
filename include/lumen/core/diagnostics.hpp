// Lumen diagnostics — include/lumen/core/diagnostics.hpp
//
// Structured log categories (SPEC §21), a secret redaction guard
// (AGENTS.md §5), and a runtime stats snapshot consumed later by REST.
//
// The snapshot takes platform-provided values (uptime, heap, reset reason)
// so the class itself is FreeRTOS-free and fully host-testable.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace lumen
{

// Structured log categories (SPEC §21).
enum class LogCategory : uint8_t
{
    kSystem = 0,
    kHardware,
    kLighting,
    kAutomation,
    kNetwork,
    kMqtt,
    kEspNow,
    kWeb,
    kOta,
    kSecurity,
};

// Human-readable name for a log category.
const char *category_name(LogCategory category);

// Secret redaction guard. Returns "[REDACTED]" when `value` is non-empty so
// credentials/tokens never reach logs (caller decides what is sensitive).
std::string redact_secret(const std::string &value);

// Masks all but the last `keep` characters of `value` with `mask_char`.
std::string mask_secret(const std::string &value, char mask_char = '*',
                        std::size_t keep = 4);

// Runtime stats snapshot. The caller (main.cpp on target) reads
// esp_timer/esp_get_free_heap_size/reset reason and passes them in; on host
// tests fixed values are used.
struct Snapshot
{
    uint32_t uptime_ms = 0;
    uint32_t free_heap_bytes = 0;
    uint32_t min_free_heap_bytes = 0;
    const char *reset_reason = "unknown";
    bool any_service_failed = false;
};

// Assemble a snapshot from platform-provided values.
Snapshot take_snapshot(uint32_t uptime_ms, uint32_t free_heap_bytes,
                       uint32_t min_free_heap_bytes, const char *reset_reason,
                       bool any_service_failed);

} // namespace lumen
