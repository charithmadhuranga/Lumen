// Lumen — effects engine implementation (src/lighting/effects_engine.cpp)
//
// Pure C++17, no I/O, no heap. Host-testable with golden values.
//
// See effects.h for the contract.
#include "lumen/lighting/effects.h"

#include <cmath>

namespace lumen
{

namespace
{

// Pseudo-random number generator for candlelight flicker (deterministic,
// seeded per-instance for reproducibility).
class SimpleRng
{
  public:
    explicit SimpleRng(uint32_t seed) : state_(seed) {}

    uint32_t next()
    {
        // Xorshift32
        state_ ^= state_ << 13;
        state_ ^= state_ >> 17;
        state_ ^= state_ << 5;
        return state_;
    }

    float next_float()
    {
        return static_cast<float>(next()) / 4294967296.0f;
    }

  private:
    uint32_t state_ = 1;
};

} // namespace

std::size_t EffectEngine::find_instance(const std::string &channel_id) const
{
    for (std::size_t i = 0; i < instances_.size(); ++i) {
        if (instances_[i].active && instances_[i].channel_id == channel_id) {
            return i;
        }
    }
    return instances_.size(); // not found
}

bool EffectEngine::start(const std::string &channel_id,
                         const EffectParams &params, uint32_t now_ms)
{
    // Cancel any existing effect for this channel first.
    cancel(channel_id);

    // Find a free slot.
    std::size_t slot = instances_.size();
    for (std::size_t i = 0; i < instances_.size(); ++i) {
        if (!instances_[i].active) {
            slot = i;
            break;
        }
    }
    if (slot == instances_.size()) {
        return false; // at capacity
    }

    EffectInstance &inst = instances_[slot];
    inst.active = true;
    inst.channel_id = channel_id;
    inst.type = params.type;
    inst.start_ms = now_ms;
    inst.duration_ms = params.duration_ms;
    inst.speed_ms = params.speed_ms;
    inst.params = params;
    return true;
}

void EffectEngine::cancel(const std::string &channel_id)
{
    std::size_t slot = find_instance(channel_id);
    if (slot != instances_.size()) {
        instances_[slot].active = false;
    }
}

void EffectEngine::cancel_all()
{
    for (auto &inst : instances_) {
        inst.active = false;
    }
}

bool EffectEngine::is_active(const std::string &channel_id) const
{
    return find_instance(channel_id) != instances_.size();
}

bool EffectEngine::compute(const std::string &channel_id, uint32_t now_ms,
                           LightState &out) const
{
    std::size_t slot = find_instance(channel_id);
    if (slot == instances_.size()) {
        return false;
    }

    const EffectInstance &inst = instances_[slot];

    // Check if effect has expired.
    uint32_t elapsed = now_ms - inst.start_ms;
    if (elapsed >= inst.duration_ms) {
        // Effect expired — but don't cancel here (caller checks is_active).
        return false;
    }

    switch (inst.type) {
    case EffectType::kBreathing:
        return compute_breathing(inst, now_ms, out);
    case EffectType::kPulse:
        return compute_pulse(inst, now_ms, out);
    case EffectType::kRainbow:
        return compute_rainbow(inst, now_ms, out);
    case EffectType::kColorCycle:
        return compute_color_cycle(inst, now_ms, out);
    case EffectType::kCandlelight:
        return compute_candlelight(inst, now_ms, out);
    case EffectType::kSunrise:
        return compute_sunrise(inst, now_ms, out);
    case EffectType::kSunset:
        return compute_sunset(inst, now_ms, out);
    case EffectType::kStrobe:
        return compute_strobe(inst, now_ms, out);
    case EffectType::kCustomSequence:
        return compute_custom_sequence(inst, now_ms, out);
    }
    return false;
}

// ========================================================================
// Effect implementations
// ========================================================================

bool EffectEngine::compute_breathing(const EffectInstance &inst,
                                     uint32_t now_ms, LightState &out) const
{
    // Slow in/out brightness oscillation between min and max brightness.
    uint32_t elapsed = now_ms - inst.start_ms;
    uint32_t cycle_ms = inst.speed_ms;
    if (cycle_ms == 0) cycle_ms = 1000;

    uint32_t phase = elapsed % cycle_ms;
    float t = static_cast<float>(phase) / static_cast<float>(cycle_ms);

    // Sine wave: 0..1 -> 0..1
    float sine = std::sin(2.0f * 3.1415926f * t);
    float factor = (sine + 1.0f) / 2.0f; // 0..1

    uint8_t min_b = inst.params.min_brightness;
    uint8_t max_b = inst.params.max_brightness;
    uint8_t bri = static_cast<uint8_t>(
        static_cast<float>(min_b) +
        factor * static_cast<float>(max_b - min_b));

    // Warm white color.
    out = LightState{};
    out.power = bri > 0;
    out.brightness = bri;
    out.red = inst.params.max_color_r;
    out.green = inst.params.max_color_g;
    out.blue = inst.params.max_color_b;
    return true;
}

bool EffectEngine::compute_pulse(const EffectInstance &inst,
                                 uint32_t now_ms, LightState &out) const
{
    // Quick on/off bursts at speed_ms interval.
    uint32_t elapsed = now_ms - inst.start_ms;
    uint32_t period_ms = inst.speed_ms;
    if (period_ms == 0) period_ms = 1000;

    uint32_t phase = elapsed % period_ms;
    // 50% duty cycle pulse.
    bool on = (phase < period_ms / 2);

    uint8_t bri = on ? inst.params.max_brightness : inst.params.min_brightness;

    out = LightState{};
    out.power = bri > 0;
    out.brightness = bri;
    out.red = inst.params.max_color_r;
    out.green = inst.params.max_color_g;
    out.blue = inst.params.max_color_b;
    return true;
}

bool EffectEngine::compute_rainbow(const EffectInstance &inst,
                                   uint32_t now_ms, LightState &out) const
{
    // Cycling hue through the spectrum at speed_ms per full cycle.
    uint32_t elapsed = now_ms - inst.start_ms;
    uint32_t cycle_ms = inst.speed_ms;
    if (cycle_ms == 0) cycle_ms = 5000;

    uint32_t phase = elapsed % cycle_ms;
    float t = static_cast<float>(phase) / static_cast<float>(cycle_ms);
    uint16_t hue = static_cast<uint16_t>(t * 360.0f) % 360;

    HsvState hsv{hue, 100, 100};
    Rgb8 rgb = hsv_to_rgb(hsv);

    out = LightState{};
    out.power = true;
    out.brightness = inst.params.max_brightness;
    out.red = rgb.r;
    out.green = rgb.g;
    out.blue = rgb.b;
    return true;
}

bool EffectEngine::compute_color_cycle(const EffectInstance &inst,
                                       uint32_t now_ms, LightState &out) const
{
    // Smooth transition between start and end hue over duration_ms.
    uint32_t elapsed = now_ms - inst.start_ms;
    float t = static_cast<float>(elapsed) / static_cast<float>(inst.duration_ms);
    if (t > 1.0f) t = 1.0f;

    uint16_t start_hue = inst.params.color_cycle_hue_start;
    uint16_t end_hue = inst.params.color_cycle_hue_end;
    uint16_t hue =
        static_cast<uint16_t>(start_hue + t * (end_hue - start_hue)) % 360;

    HsvState hsv{hue, 100, 100};
    Rgb8 rgb = hsv_to_rgb(hsv);

    out = LightState{};
    out.power = true;
    out.brightness = inst.params.max_brightness;
    out.red = rgb.r;
    out.green = rgb.g;
    out.blue = rgb.b;
    return true;
}

bool EffectEngine::compute_candlelight(const EffectInstance &inst,
                                       uint32_t now_ms, LightState &out) const
{
    // Flickering brightness with warm color.
    uint32_t elapsed = now_ms - inst.start_ms;
    SimpleRng rng(static_cast<uint32_t>(elapsed / 50));

    // Base brightness with random flicker.
    uint8_t base = (inst.params.min_brightness + inst.params.max_brightness) / 2;
    int8_t flicker = static_cast<int8_t>(
        (rng.next_float() - 0.5f) * 30.0f); // ±15 flicker
    int8_t bri_int = static_cast<int8_t>(base) + flicker;
    uint8_t bri = static_cast<uint8_t>(
        clamp(static_cast<int>(bri_int),
              static_cast<int>(inst.params.min_brightness),
              static_cast<int>(inst.params.max_brightness)));

    out = LightState{};
    out.power = bri > 0;
    out.brightness = bri;
    out.red = inst.params.max_color_r;
    out.green = inst.params.max_color_g;
    out.blue = inst.params.max_color_b;
    return true;
}

bool EffectEngine::compute_sunrise(const EffectInstance &inst,
                                   uint32_t now_ms, LightState &out) const
{
    // Gradual brightening from dark to warm over duration_ms.
    uint32_t elapsed = now_ms - inst.start_ms;
    float t = static_cast<float>(elapsed) / static_cast<float>(inst.duration_ms);
    if (t > 1.0f) t = 1.0f;

    // Smooth ease-in for gentle start.
    float eased = t * t;

    uint8_t bri = static_cast<uint8_t>(
        eased * static_cast<float>(inst.params.max_brightness));

    // Warm color that intensifies.
    out = LightState{};
    out.power = bri > 0;
    out.brightness = bri;
    out.red = inst.params.max_color_r;
    out.green = static_cast<uint8_t>(eased * 200.0f);
    out.blue = static_cast<uint8_t>(eased * 50.0f);
    return true;
}

bool EffectEngine::compute_sunset(const EffectInstance &inst,
                                  uint32_t now_ms, LightState &out) const
{
    // Gradual dimming from bright to warm/off over duration_ms.
    uint32_t elapsed = now_ms - inst.start_ms;
    float t = static_cast<float>(elapsed) / static_cast<float>(inst.duration_ms);
    if (t > 1.0f) t = 1.0f;

    uint8_t bri = static_cast<uint8_t>(
        (1.0f - t) * static_cast<float>(inst.params.max_brightness));

    out = LightState{};
    out.power = bri > 0;
    out.brightness = bri;
    out.red = inst.params.max_color_r;
    out.green = static_cast<uint8_t>(t * 100.0f);
    out.blue = static_cast<uint8_t>(t * 30.0f);
    return true;
}

bool EffectEngine::compute_strobe(const EffectInstance &inst,
                                  uint32_t now_ms, LightState &out) const
{
    // Rapid on/off flashing.
    uint32_t elapsed = now_ms - inst.start_ms;
    uint32_t period_ms = inst.params.strobe_period_ms;
    if (period_ms == 0) period_ms = 500;

    uint32_t phase = elapsed % period_ms;
    uint32_t on_time = (period_ms * inst.params.strobe_duty_cycle) / 100;
    bool on = (phase < on_time);

    uint8_t bri = on ? inst.params.max_brightness : inst.params.min_brightness;

    out = LightState{};
    out.power = bri > 0;
    out.brightness = bri;
    out.red = inst.params.max_color_r;
    out.green = inst.params.max_color_g;
    out.blue = inst.params.max_color_b;
    return true;
}

bool EffectEngine::compute_custom_sequence(const EffectInstance &inst,
                                           uint32_t now_ms,
                                           LightState &out) const
{
    // Custom sequence: user-defined states over time.
    // For now, treat as color cycle between min and max color.
    (void)inst;
    (void)now_ms;
    out = LightState{};
    out.power = false;
    return false;
}

} // namespace lumen
