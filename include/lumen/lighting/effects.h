// Lumen — effects engine (include/lumen/lighting/effects.h)
//
// Effects are resource-managed, cancellable instances that produce LightState
// values over time. Any direct command cancels an active effect for the
// channel (SPEC §8.6).
//
// Supported effects (SPEC §8.6): breathing, pulse, rainbow, color cycle,
// candlelight, sunrise, sunset, strobe, custom sequence.
//
// Header-only for host-testability; no heap, no blocking.
#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <array>

#include "lumen/lighting/color_engine.h"
#include "lumen/lighting/transition.h"

namespace lumen
{

// Effect types (SPEC §8.6).
enum class EffectType : uint8_t
{
    kBreathing = 0,   // Slow in/out brightness oscillation
    kPulse,           // Quick on/off bursts
    kRainbow,         // Cycling hue through the spectrum
    kColorCycle,      // Smooth transition between colors
    kCandlelight,     // Flickering brightness with warm color
    kSunrise,         // Gradual brightening from dark to warm
    kSunset,          // Gradual dimming from bright to warm/off
    kStrobe,          // Rapid on/off flashing
    kCustomSequence,  // User-defined sequence of states
};

// Effect parameters.
struct EffectParams
{
    EffectType type = EffectType::kBreathing;
    uint32_t duration_ms = 5000;   // total effect duration
    uint32_t speed_ms = 1000;      // speed parameter (effect-specific)
    uint8_t min_brightness = 10;   // for breathing/candle
    uint8_t max_brightness = 100;  // for breathing/candle
    uint8_t min_color_r = 255;     // for candlelight (warm)
    uint8_t min_color_g = 180;
    uint8_t min_color_b = 50;
    uint8_t max_color_r = 255;
    uint8_t max_color_g = 220;
    uint8_t max_color_b = 100;
    uint16_t color_cycle_hue_start = 0;
    uint16_t color_cycle_hue_end = 360;
    uint8_t strobe_duty_cycle = 50; // percent on
    uint32_t strobe_period_ms = 500;
};

// An active effect instance.
struct EffectInstance
{
    bool active = false;
    EffectType type = EffectType::kBreathing;
    std::string channel_id;
    uint32_t start_ms = 0;
    uint32_t duration_ms = 0;
    uint32_t speed_ms = 1000;
    EffectParams params{};
};

// Effect engine: bounded effect instances, one per channel maximum.
class EffectEngine
{
  public:
    static constexpr std::size_t kMaxEffects = 16;

    EffectEngine() = default;

    // Start an effect on a channel. Cancels any existing effect for that
    // channel. Returns false when at capacity.
    bool start(const std::string &channel_id, const EffectParams &params,
               uint32_t now_ms);

    // Cancel the effect on a channel (called by any direct command).
    void cancel(const std::string &channel_id);

    // Compute the current LightState for a channel at `now_ms`.
    // Returns true if an effect is active and produced a state.
    bool compute(const std::string &channel_id, uint32_t now_ms,
                 LightState &out) const;

    // Cancel all effects.
    void cancel_all();

    bool is_active(const std::string &channel_id) const;

  private:
    std::array<EffectInstance, kMaxEffects> instances_{};

    std::size_t find_instance(const std::string &channel_id) const;

    // Compute effect-specific state.
    bool compute_breathing(const EffectInstance &inst, uint32_t now_ms,
                           LightState &out) const;
    bool compute_pulse(const EffectInstance &inst, uint32_t now_ms,
                       LightState &out) const;
    bool compute_rainbow(const EffectInstance &inst, uint32_t now_ms,
                         LightState &out) const;
    bool compute_color_cycle(const EffectInstance &inst, uint32_t now_ms,
                             LightState &out) const;
    bool compute_candlelight(const EffectInstance &inst, uint32_t now_ms,
                             LightState &out) const;
    bool compute_sunrise(const EffectInstance &inst, uint32_t now_ms,
                         LightState &out) const;
    bool compute_sunset(const EffectInstance &inst, uint32_t now_ms,
                        LightState &out) const;
    bool compute_strobe(const EffectInstance &inst, uint32_t now_ms,
                        LightState &out) const;
    bool compute_custom_sequence(const EffectInstance &inst, uint32_t now_ms,
                                 LightState &out) const;
};

} // namespace lumen
