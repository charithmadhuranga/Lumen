// Lumen event model — typed ids, priorities and a FIXED-SIZE payload.
//
// Contract (stable, per .opencode/plans/phase-01-core-firmware.md):
// - No heap allocation, no heap strings in events (AGENTS.md §5).
// - EventId values are append-only: never reorder or reuse (protocol
//   stability for ESP-NOW envelopes in Phase 10 that embed event ids).
// - sizeof(Event) is asserted small so queue storage is predictable.
#pragma once

#include <cstdint>

namespace lumen
{

enum class EventId : uint16_t {
    // --- core (Phase 1) ---
    SystemBoot = 0,
    SystemShutdown,
    ConfigChanged,
    ConfigLoadFailed,
    StorageError,
    DiagnosticsTick, // telemetry

    // --- hardware (Phase 2/3) ---
    HardwareFault,      // critical
    InputStateChanged,  // button/input edge
    PeripheralOffline,  // I2C device stopped answering

    // --- lighting (Phase 4/5) ---
    LightStateChanged, // telemetry, coalesced
    SceneActivated,
    GroupStateChanged,

    // --- automation (Phase 9) ---
    AutomationTriggered,
    AutomationFailed,

    // --- networking (Phase 10) ---
    PeerOnline,
    PeerOffline,
    CommandConflict,

    // --- sentinel ---
    Any, // subscribe-only wildcard; never posted
};

enum class EventPriority : uint8_t {
    Telemetry = 0, // may be coalesced when queues are full
    Normal = 1,
    Critical = 2, // never silently dropped (evicts lower priority, else counted)
};

// Fixed-size payload (16 bytes). Fields are documented per consumer; unused
// bytes stay zero. No pointers to transient data — ever.
struct EventPayload {
    union Data {
        struct
        {
            uint32_t code;  // e.g. esp_err_t or app error code
            int32_t value;
        } status;
        struct
        {
            uint32_t doc_hash;  // FNV-1a of the document path (lumen identity)
            uint16_t doc_len;
            int32_t schema_version;
        } config;
        struct
        {
            uint16_t channel; // logical light channel
            uint8_t on;
            uint8_t brightness;
            uint8_t rgb[3];
            uint8_t pad;
        } light;
        struct
        {
            uint16_t input;
            uint8_t active;
            uint8_t pad;
        } input;
        uint8_t bytes[16];
    } data;
};
static_assert(sizeof(EventPayload::Data) <= 16, "event payload must stay fixed-size");

struct Event {
    EventId id = EventId::SystemBoot;
    EventPriority priority = EventPriority::Normal;
    uint16_t reserved = 0;
    uint32_t timestamp_ms = 0; // uptime when posted (bus-filled)
    uint32_t sequence = 0;     // bus-assigned, strictly increasing (bus-filled)
    EventPayload payload{};
};
static_assert(sizeof(Event) <= 32, "event must stay fixed-size for predictable queues");

// True when `id` is a real event (not the subscribe-only wildcard).
constexpr bool is_real_event(EventId id)
{
    return id != EventId::Any;
}

} // namespace lumen
