// Lumen — transition engine (include/lumen/lighting/transition.h)
//
// Non-blocking interpolation of brightness, RGB, CCT over a configurable
// duration (SPEC §8.5). Driven by a tick/step schedule — no delay loops in
// the lighting task (AGENTS.md §5). Target ≥30 updates/s where hardware
// permits (SPEC §24).
//
// A newer command replaces an active transition for the same channel.
// Transitions are cancellable.
//
// All math is pure and host-testable. Functions are inline/header-only so
// all translation units share one copy — no cross-object link dependency.
#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <cmath>

#include "lumen/lighting/color_engine.h"

namespace lumen
{

// Easing function type: t in 0..1000 (permille), returns 0..1000.
using EaseFn = uint16_t (*)(uint16_t t);

// Linear easing.
inline uint16_t ease_linear(uint16_t t)
{
    return t;
}

// Smooth step easing (3t^2 - 2t^3).
inline uint16_t ease_smooth(uint16_t t)
{
    float ft = static_cast<float>(t) / 1000.0f;
    float result = 3.0f * ft * ft - 2.0f * ft * ft * ft;
    return static_cast<uint16_t>(
        clamp(static_cast<int>(result * 1000.0f + 0.5f), 0, 1000));
}

// Ease-in-out cubic.
inline uint16_t ease_inout_cubic(uint16_t t)
{
    float ft = static_cast<float>(t) / 1000.0f;
    float result;
    if (ft < 0.5f) {
        result = 4.0f * ft * ft * ft;
    } else {
        float f = 2.0f * ft - 2.0f;
        result = 0.5f * f * f * f + 1.0f;
    }
    return static_cast<uint16_t>(
        clamp(static_cast<int>(result * 1000.0f + 0.5f), 0, 1000));
}

// A transition in progress.
struct Transition
{
    bool active = false;
    uint32_t start_ms = 0;      // when the transition started
    uint32_t duration_ms = 0;   // total duration
    LightState start_state{};   // state at transition start
    LightState end_state{};     // target state
    EaseFn ease = ease_linear;  // easing function
};

// Lighting command envelope (SPEC §8.1 / contract). Producers enqueue these;
// the lighting task is the only executor.
struct LightCommand
{
    std::string channel_id;
    LightState state{};         // target state
    uint32_t transition_ms = 0; // 0 = instant
    std::string origin;         // "rest", "ws", "mqtt", "automation", etc.
    std::string command_id;     // for Phase 11 dedup
};

// Result of applying a command or transition tick.
struct ApplyResult
{
    bool state_changed = false; // whether any hardware write is needed
    LightState new_state{};     // the new applied state
};

// Linear interpolation between a and b by fraction t (0..1000).
template <typename T>
inline T lerp(T a, T b, uint16_t t)
{
    if (t <= 0) return a;
    if (t >= 1000) return b;
    int64_t diff = static_cast<int64_t>(b) - static_cast<int64_t>(a);
    int64_t result = static_cast<int64_t>(a) +
                     (diff * static_cast<int64_t>(t)) / 1000;
    return static_cast<T>(result);
}

// Compute the interpolated state at time `now_ms` for a transition.
// Returns true if the transition is still active (not yet complete).
inline bool transition_tick(Transition &t, uint32_t now_ms, LightState &out)
{
    if (!t.active) {
        return false;
    }

    uint32_t elapsed = now_ms - t.start_ms;
    uint16_t progress;

    if (elapsed >= t.duration_ms) {
        progress = 1000;
    } else if (t.duration_ms == 0) {
        progress = 1000;
    } else {
        progress = static_cast<uint16_t>(
            clamp(static_cast<int>((static_cast<uint64_t>(elapsed) * 1000) /
                                   static_cast<uint64_t>(t.duration_ms)),
                  0, 1000));
    }

    // Apply easing to progress.
    uint16_t eased = t.ease(progress);

    // Interpolate each channel.
    out.power = (eased >= 500) ? t.end_state.power : t.start_state.power;
    out.brightness = lerp(t.start_state.brightness, t.end_state.brightness, eased);
    out.red = lerp(t.start_state.red, t.end_state.red, eased);
    out.green = lerp(t.start_state.green, t.end_state.green, eased);
    out.blue = lerp(t.start_state.blue, t.end_state.blue, eased);
    out.white = lerp(t.start_state.white, t.end_state.white, eased);
    out.color_temperature = lerp(t.start_state.color_temperature,
                                 t.end_state.color_temperature, eased);

    bool complete = (progress >= 1000);
    if (complete) {
        t.active = false;
        out = t.end_state; // snap to exact end state
    }
    return !complete;
}

// Apply a command: if transition_ms == 0, instant. Otherwise start a
// transition from the current applied state to the target.
inline ApplyResult apply_command(Transition &t, const LightCommand &cmd,
                                 const LightState &current_applied,
                                 uint32_t now_ms)
{
    ApplyResult result{};

    if (cmd.transition_ms == 0 || cmd.transition_ms == 1) {
        // Instant change.
        t.active = false;
        result.new_state = cmd.state;
        result.state_changed = true;
        return result;
    }

    // Start a transition from current state to target.
    t.active = true;
    t.start_ms = now_ms;
    t.duration_ms = cmd.transition_ms;
    t.start_state = current_applied;
    t.end_state = cmd.state;
    t.ease = ease_smooth; // default easing

    // Compute first tick immediately.
    LightState first{};
    bool still_active = transition_tick(t, now_ms, first);
    result.new_state = first;
    result.state_changed = true;
    (void)still_active;
    return result;
}

} // namespace lumen
