// Lumen — lighting engine (include/lumen/lighting/lighting_task.h)
//
// The lighting service is the sole executor of lighting commands (SPEC §8,
// §23). Producers (REST/WS/MQTT/inputs/automation/scenes) enqueue
// LightCommandEnvelopes into a bounded queue; the lighting task drains it,
// resolves the requested state, then advances transitions and effects every
// tick and writes changed outputs through driver adapters. PCA9685 writes
// are batched: one flush per device per tick.
//
// Pure C++ core (host-testable with a mock IHardwareManager). The FreeRTOS
// loop that calls run_once() lives in the lighting service (main firmware).
//
// Threading: enqueue() is safe from any task (internal queue mutex). All
// other methods must be called with lumen::model_mutex() held.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>

#include "lumen/hardware/hal/i_hardware_manager.h"
#include "lumen/lighting/channel.h"
#include "lumen/lighting/driver_adapter.h"
#include "lumen/lighting/effects.h"
#include "lumen/lighting/transition.h"

namespace lumen
{

// Maximum number of commands the queue can hold (bounded, no dynamic alloc).
static constexpr std::size_t kLightingCommandQueueSize = 64;

// A command pushed to the lighting task by producers.
struct LightCommandEnvelope
{
    LightCommand command;
    uint32_t posted_ms = 0; // uptime when posted
    uint64_t posted_us = 0; // stamped by enqueue() (latency measurement)
};

enum class EnqueueResult : uint8_t
{
    kOk = 0,
    kQueueFull, // bounded queue — observable via stats().queue_dropped
};

// Why a listener is being notified.
enum class LightChange : uint8_t
{
    kRequested = 0, // a command changed the target state
    kSettled,       // a transition/effect finished; outputs == requested
    kConfig,        // channel added/updated/removed
};

using LightListener = void (*)(const Channel &channel, LightChange change, void *user);

struct LightingStats
{
    uint32_t commands_processed = 0;
    uint32_t commands_rejected = 0; // unknown channel
    uint32_t queue_dropped = 0;     // enqueue() on a full queue
    uint32_t hardware_writes = 0;
    uint32_t hardware_errors = 0;
    uint32_t ticks = 0;
    uint32_t max_tick_us = 0;
    uint32_t tick_overruns = 0;          // ticks longer than the loop period
    // Command latency: enqueue -> hardware flush that applied it (SPEC §24).
    uint32_t cmd_latency_last_us = 0;
    uint32_t cmd_latency_max_us = 0;
    uint64_t cmd_latency_sum_us = 0;
    uint32_t cmd_latency_count = 0;
};

class LightingTask
{
  public:
    LightingTask() = default;

    // --- hardware binding -------------------------------------------------
    // Bind (or rebind after a profile apply) every channel to its driver.
    // Channels whose driver is missing are marked unavailable but keep
    // their state, so they recover on the next attach.
    void attach_hardware(IHardwareManager *hw);
    // Drive every bound output to its safe state and drop the adapters.
    void detach_hardware();
    bool hardware_attached() const { return hw_ != nullptr; }

    // --- commands ---------------------------------------------------------
    EnqueueResult enqueue(const LightCommandEnvelope &cmd);
    // Drain up to `max_commands` queued commands (resolves requested state,
    // starts transitions/effects). Returns how many were processed.
    std::size_t process(uint32_t now_ms, std::size_t max_commands);
    // Advance transitions/effects and write changed outputs.
    void tick(uint32_t now_ms);
    // process() + tick().
    void run_once(uint32_t now_ms);
    std::size_t queue_depth() const;

    // --- channel registry -------------------------------------------------
    bool register_channel(const Channel &channel);
    bool update_channel(const Channel &channel); // keeps state; rebinds driver
    bool remove_channel(const std::string &channel_id);
    ChannelManager &channel_manager() { return channel_manager_; }
    const ChannelManager &channel_manager() const { return channel_manager_; }

    bool get_requested(const std::string &channel_id, LightState &out) const;
    bool get_applied(const std::string &channel_id, LightState &out) const;
    bool hardware_ok(const std::string &channel_id) const;
    bool animating(const std::string &channel_id) const; // transition/effect running
    std::string active_effect(const std::string &channel_id) const;

    // Set requested + applied directly (boot-time restore). No transition.
    bool restore_state(const std::string &channel_id, const LightState &state);

    void set_listener(LightListener listener, void *user)
    {
        listener_ = listener;
        listener_user_ = user;
    }

    // True once since the last call if any requested state changed
    // (drives debounced persistence of last-known state).
    bool take_state_dirty();

    const LightingStats &stats() const { return stats_; }
    // Microsecond clock for latency stamps (nullptr on host tests = off).
    void set_clock_us(uint64_t (*clock)()) { clock_us_ = clock; }

    void record_tick_us(uint32_t us, uint32_t period_us = 0)
    {
        if (period_us != 0 && us > period_us) stats_.tick_overruns++;
        if (us > stats_.max_tick_us) stats_.max_tick_us = us;
    }

  private:
    struct Slot
    {
        Transition transition{};
        LightState target{};        // exact state to snap to at the end
        std::unique_ptr<IDriverAdapter> adapter;
        bool adapter_ok = false;
        LightState written{};
        bool written_valid = false;
        std::string effect;         // active effect name ("" = none)
    };

    void bind_slot(std::size_t index);
    void notify(std::size_t index, LightChange change);
    void apply_command(const LightCommand &cmd, uint32_t now_ms);
    bool write_slot(std::size_t index, const LightState &out);

    ChannelManager channel_manager_;
    std::array<Slot, ChannelManager::kMaxChannels> slots_{};
    EffectEngine effects_;
    IHardwareManager *hw_ = nullptr;

    // Bounded command queue (ring buffer) + its own lock so producers never
    // need the model lock to enqueue.
    mutable std::mutex queue_mutex_;
    std::array<LightCommandEnvelope, kLightingCommandQueueSize> queue_{};
    std::size_t queue_head_ = 0;
    std::size_t queue_tail_ = 0;
    std::size_t queue_count_ = 0;

    LightListener listener_ = nullptr;
    uint64_t (*clock_us_)() = nullptr;
    uint64_t oldest_pending_us_ = 0; // oldest command applied since the last flush
    void *listener_user_ = nullptr;
    bool state_dirty_ = false;
    LightingStats stats_{};
};

// Effect names accepted by LightCommand::effect.
bool effect_type_from_string(const std::string &name, EffectType &out);
const char *effect_type_name(EffectType type);

} // namespace lumen
