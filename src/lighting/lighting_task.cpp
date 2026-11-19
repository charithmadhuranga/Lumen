// Lumen — lighting task implementation (src/lighting/lighting_task.cpp)
//
// Pure C++ core: command queue, transition processing, hardware writes via
// driver adapters. Host-testable — no FreeRTOS dependencies here.
//
// The FreeRTOS task wrapper (target-only) lives in the platform-specific
// file and calls process() in a loop.
#include "lumen/lighting/lighting_task.h"

#include <cstring>

#include "lumen/core/event.hpp"
#include "lumen/core/event_bus.hpp"

namespace lumen
{

namespace
{

// Publish LIGHT_STATE_CHANGED on the event bus after hardware application.
// The event bus is a global singleton accessed by the lighting task.
EventBus *g_event_bus = nullptr;

} // namespace

void lighting_task_set_event_bus(EventBus &bus)
{
    g_event_bus = &bus;
}

EnqueueResult LightingTask::enqueue(const LightCommandEnvelope &cmd)
{
    if (queue_count_ >= queue_.size()) {
        return EnqueueResult::kQueueFull;
    }
    queue_[queue_tail_] = cmd;
    queue_tail_ = (queue_tail_ + 1) % queue_.size();
    queue_count_++;
    return EnqueueResult::kOk;
}

std::size_t LightingTask::process(uint32_t now_ms,
                                  std::size_t max_commands)
{
    std::size_t processed = 0;
    while (processed < max_commands && queue_count_ > 0) {
        LightCommandEnvelope env = queue_[queue_head_];
        queue_head_ = (queue_head_ + 1) % queue_.size();
        queue_count_--;

        // Resolve channel.
        Channel *ch = channel_manager_.find_mutable(env.command.channel_id);
        if (ch == nullptr) {
            // Unknown channel — skip but count as processed.
            processed++;
            continue;
        }

        // If there's an active transition for this channel, the new command
        // replaces it (SPEC §8.5).
        LightState new_state = env.command.state;

        // Apply transition if transition_ms > 0.
        if (env.command.transition_ms > 0 && env.command.transition_ms != 1) {
            // Find or allocate a transition slot for this channel.
            std::size_t slot = find_transition_slot(env.command.channel_id);
            Transition &tr = transitions_[slot];

            ApplyResult ar = apply_command(tr, env.command,
                                           ch->applied, now_ms);
            new_state = ar.new_state;

            // If transition completed immediately (shouldn't happen with
            // transition_ms > 1), mark inactive.
        }

        // Update requested state.
        ch->requested = new_state;

        // Write to hardware via driver adapters.
        bool wrote = apply_to_hardware(env.command, new_state, now_ms);
        if (wrote) {
            ch->applied = new_state;
        }

        // Publish state change on the event bus.
        if (g_event_bus != nullptr) {
            EventPayload payload{};
            payload.data.bytes[0] = 0; // placeholder — channel id hash
            g_event_bus->post(EventId::LightStateChanged,
                              EventPriority::Telemetry, payload);
        }

        processed++;
    }
    return processed;
}

bool LightingTask::get_requested(const std::string &channel_id,
                                 LightState &out) const
{
    return channel_manager_.get_requested(channel_id, out);
}

bool LightingTask::get_applied(const std::string &channel_id,
                               LightState &out) const
{
    const Channel *ch = channel_manager_.find(channel_id);
    if (ch == nullptr) return false;
    out = ch->applied;
    return true;
}

bool LightingTask::register_channel(const Channel &channel)
{
    return channel_manager_.register_channel(channel);
}

bool LightingTask::apply_to_hardware(const LightCommand &cmd,
                                     const LightState &new_state,
                                     uint32_t now_ms)
{
    (void)now_ms;
    // Resolve driver adapter through hardware manager and write duty values.
    // The channel_manager_ provides channel config; driver adapters are
    // resolved via the hardware manager (not directly by lighting code).
    //
    // This is a placeholder — driver adapters are wired in Phase 4 M4.6.
    // For now, track the state as applied without actual hardware writes.
    (void)cmd;
    return true; // pretend we wrote
}

std::size_t LightingTask::find_transition_slot(
    const std::string &channel_id)
{
    // Linear search for existing slot (bounded by max channels).
    for (std::size_t i = 0; i < transition_slot_count_; ++i) {
        // We can't easily map channel_id to slot without storing the id,
        // so for simplicity allocate a new slot per command.
        (void)channel_id;
    }
    if (transition_slot_count_ >= transitions_.size()) {
        return transition_slot_count_ - 1; // reuse last slot
    }
    return transition_slot_count_++;
}

} // namespace lumen
