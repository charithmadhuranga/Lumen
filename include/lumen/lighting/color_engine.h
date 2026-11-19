// Lumen — color engine interface (include/lumen/lighting/color_engine.h)
//
// Pure, host-testable color conversion functions (SPEC §8.4). No I/O, no
// heap, no blocking. Every conversion is deterministic with golden values
// suitable for host unit tests. Functions are inline/header-only so all
// translation units share one copy — no cross-object link dependency.
//
// Rules (AGENTS.md §7, SPEC §8.4):
// - One code path for brightness/color applied across LEDC, PCA9685 and RMT.
// - Brightness compensation uses the color engine's compensation — never a
//   raw multiplier that clips colors.
// - RGBW handling preserves white channel; CCT mixes kelvin→RGB.
// - Gamma correction and per-channel calibration/limits applied last.
#pragma once

#include <cstdint>
#include <cmath>
#include <cstring>

namespace lumen {

// Normalized internal lighting state (SPEC §8.3). All channels share this
// representation; drivers translate to their native duty/color values.
struct LightState {
    bool power = false;            // on/off
    uint8_t brightness = 0;        // 0..100 percent
    uint8_t red = 0;               // 0..255
    uint8_t green = 0;             // 0..255
    uint8_t blue = 0;              // 0..255
    uint8_t white = 0;             // 0..255 (RGBW only)
    uint16_t color_temperature = 0;// kelvin, 1000..20000 (CCT only)
};

// HSV representation used internally by conversions.
struct HsvState {
    uint16_t hue = 0;       // 0..359 degrees
    uint8_t saturation = 0; // 0..100 percent
    uint8_t value = 0;      // 0..100 percent (brightness)
};

// CCT (kelvin) representation.
struct CctState {
    uint16_t kelvin = 0;    // 1000..20000
    uint8_t brightness = 0; // 0..100 percent
};

// Gamma curve. lookup[i] = gamma-corrected 8-bit value for input i (0..255).
inline const uint8_t *gamma8_table()
{
        static const uint8_t table[256] = {
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1,
        1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2,
        3, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 6, 6, 6,
        6, 7, 7, 7, 8, 8, 8, 9, 9, 9, 10, 10, 11, 11, 11, 12,
        12, 13, 13, 13, 14, 14, 15, 15, 16, 16, 17, 17, 18, 18, 19, 19,
        20, 20, 21, 22, 22, 23, 23, 24, 25, 25, 26, 26, 27, 28, 28, 29,
        30, 30, 31, 32, 33, 33, 34, 35, 35, 36, 37, 38, 39, 39, 40, 41,
        42, 43, 43, 44, 45, 46, 47, 48, 49, 49, 50, 51, 52, 53, 54, 55,
        56, 57, 58, 59, 60, 61, 62, 63, 64, 65, 66, 67, 68, 69, 70, 71,
        73, 74, 75, 76, 77, 78, 79, 81, 82, 83, 84, 85, 87, 88, 89, 90,
        91, 93, 94, 95, 97, 98, 99, 100, 102, 103, 105, 106, 107, 109, 110, 111,
        113, 114, 116, 117, 119, 120, 121, 123, 124, 126, 127, 129, 130, 132, 133, 135,
        137, 138, 140, 141, 143, 145, 146, 148, 149, 151, 153, 154, 156, 158, 159, 161,
        163, 165, 166, 168, 170, 172, 173, 175, 177, 179, 181, 182, 184, 186, 188, 190,
        192, 194, 196, 197, 199, 201, 203, 205, 207, 209, 211, 213, 215, 217, 219, 221,
        223, 225, 227, 229, 231, 234, 236, 238, 240, 242, 244, 246, 248, 251, 253, 255,
    };
    return table;
}

// Apply gamma to an 8-bit channel value.
constexpr uint8_t apply_gamma(uint8_t value)
{
    return gamma8_table()[value];
}

// Clamp a value to [lo, hi].
template <typename T>
constexpr T clamp(T value, T lo, T hi)
{
    if (value < lo) return lo;
    if (value > hi) return hi;
    return value;
}

// Fast floor for positive floats.
inline int fast_floor(float x)
{
    return static_cast<int>(x);
}

// ========================================================================
// RGB <-> HSV conversions
// ========================================================================

// Convert RGB (0..255 each) to HSV. hue in 0..359, saturation/value 0..100.
inline HsvState rgb_to_hsv(uint8_t r, uint8_t g, uint8_t b)
{
    HsvState hsv{};

    float rf = static_cast<float>(r) / 255.0f;
    float gf = static_cast<float>(g) / 255.0f;
    float bf = static_cast<float>(b) / 255.0f;

    float cmax = rf > gf ? (rf > bf ? rf : bf) : (gf > bf ? gf : bf);
    float cmin = rf < gf ? (rf < bf ? rf : bf) : (gf < bf ? gf : bf);
    float delta = cmax - cmin;

    hsv.value = static_cast<uint8_t>(
        clamp(static_cast<int>(cmax * 100.0f + 0.5f), 0, 100));

    if (delta < 0.001f) {
        hsv.hue = 0;
        hsv.saturation = 0;
        return hsv;
    }

    hsv.saturation = static_cast<uint8_t>(
        clamp(static_cast<int>((delta / cmax) * 100.0f + 0.5f), 0, 100));

    float h = 0.0f;
    if (cmax == rf) {
        h = 60.0f * ((gf - bf) / delta);
        if (h < 0.0f) h += 360.0f;
    } else if (cmax == gf) {
        h = 60.0f * ((bf - rf) / delta) + 120.0f;
    } else {
        h = 60.0f * ((rf - gf) / delta) + 240.0f;
    }
    hsv.hue = static_cast<uint16_t>(clamp(static_cast<int>(h + 0.5f), 0, 359));

    return hsv;
}

// Convert HSV back to RGB. Returns {r, g, b} each 0..255.
struct Rgb8 { uint8_t r, g, b; };

inline Rgb8 hsv_to_rgb(const HsvState &hsv)
{
    Rgb8 rgb{0, 0, 0};

    float h = static_cast<float>(hsv.hue);
    float s = static_cast<float>(hsv.saturation) / 100.0f;
    float v = static_cast<float>(hsv.value) / 100.0f;

    if (s < 0.001f) {
        uint8_t gray = static_cast<uint8_t>(
            clamp(static_cast<int>(v * 255.0f + 0.5f), 0, 255));
        rgb.r = gray;
        rgb.g = gray;
        rgb.b = gray;
        return rgb;
    }

    h = std::fmod(h, 360.0f);
    float h60 = h / 60.0f;
    int hi = fast_floor(h60);
    float f = h60 - static_cast<float>(hi);
    float p = v * (1.0f - s);
    float q = v * (1.0f - s * f);
    float t = v * (1.0f - s * (1.0f - f));

    float rf, gf, bf;
    switch (hi % 6) {
    case 0: rf = v; gf = t; bf = p; break;
    case 1: rf = q; gf = v; bf = p; break;
    case 2: rf = p; gf = v; bf = t; break;
    case 3: rf = p; gf = q; bf = v; break;
    case 4: rf = t; gf = p; bf = v; break;
    default: rf = v; gf = p; bf = q; break;
    }

    rgb.r = static_cast<uint8_t>(
        clamp(static_cast<int>(rf * 255.0f + 0.5f), 0, 255));
    rgb.g = static_cast<uint8_t>(
        clamp(static_cast<int>(gf * 255.0f + 0.5f), 0, 255));
    rgb.b = static_cast<uint8_t>(
        clamp(static_cast<int>(bf * 255.0f + 0.5f), 0, 255));
    return rgb;
}

// ========================================================================
// RGBW handling
// ========================================================================

struct Rgbw8 { uint8_t r, g, b, w; };

// Split an RGB color into RGBW: compute the white channel as the min of
// (r,g,b), then subtract white from each color channel.
// Returns {r, g, b, w} each 0..255.
inline Rgbw8 rgb_to_rgbw(uint8_t r, uint8_t g, uint8_t b)
{
    Rgbw8 rgbw{};
    uint8_t w = r < g ? (r < b ? r : b) : (g < b ? g : b);
    rgbw.w = w;
    rgbw.r = r - w;
    rgbw.g = g - w;
    rgbw.b = b - w;
    return rgbw;
}

// Merge RGBW back to RGB by adding white to each channel and clamping to 255.
inline Rgb8 rgbw_to_rgb(uint8_t r, uint8_t g, uint8_t b, uint8_t w)
{
    Rgb8 rgb{};
    rgb.r = static_cast<uint8_t>(
        clamp(static_cast<int>(r) + static_cast<int>(w), 0, 255));
    rgb.g = static_cast<uint8_t>(
        clamp(static_cast<int>(g) + static_cast<int>(w), 0, 255));
    rgb.b = static_cast<uint8_t>(
        clamp(static_cast<int>(b) + static_cast<int>(w), 0, 255));
    return rgb;
}

// ========================================================================
// CCT (kelvin) <-> RGB
// ========================================================================

// Approximate blackbody radiation: kelvin (1000..20000) -> RGB (0..255 each).
inline Rgb8 kelvin_to_rgb(uint16_t kelvin)
{
    kelvin = clamp(kelvin, uint16_t(1000), uint16_t(20000));

    float t = static_cast<float>(kelvin) / 100.0f;
    float r, g, b;

    if (t <= 66.0f) {
        r = 255.0f;
    } else {
        r = 329.698727446f * std::pow(t - 60.0f, -0.1332047592f);
        r = clamp(static_cast<int>(r + 0.5f), 0, 255);
    }

    if (t <= 66.0f) {
        g = 99.4708025861f * std::log(t) - 161.1195681661f;
        g = clamp(static_cast<int>(g + 0.5f), 0, 255);
    } else {
        g = 288.1221695283f * std::pow(t - 60.0f, -0.0755148492f);
        g = clamp(static_cast<int>(g + 0.5f), 0, 255);
    }

    if (t >= 66.0f) {
        b = 255.0f;
    } else if (t <= 19.0f) {
        b = 0.0f;
    } else {
        b = 138.5177312231f * std::log(t - 10.0f) - 305.0447927307f;
        b = clamp(static_cast<int>(b + 0.5f), 0, 255);
    }

    Rgb8 rgb{};
    rgb.r = static_cast<uint8_t>(r);
    rgb.g = static_cast<uint8_t>(g);
    rgb.b = static_cast<uint8_t>(b);
    return rgb;
}

// Approximate inverse: RGB -> kelvin. Returns 0 when color is far from the
// blackbody locus (highly saturated colors have no meaningful kelvin).
inline uint16_t rgb_to_kelvin(uint8_t r, uint8_t g, uint8_t b)
{
    uint16_t best_k = 1000;
    float best_dist = 1e9f;

    for (uint16_t k = 1000; k <= 20000; k += 100) {
        Rgb8 ref = kelvin_to_rgb(k);
        float dr = static_cast<float>(r) - static_cast<float>(ref.r);
        float dg = static_cast<float>(g) - static_cast<float>(ref.g);
        float db = static_cast<float>(b) - static_cast<float>(ref.b);
        float dist = dr * dr + dg * dg + db * db;
        if (dist < best_dist) {
            best_dist = dist;
            best_k = k;
        }
    }

    if (best_dist > 4000.0f) {
        return 0;
    }
    return best_k;
}

// ========================================================================
// Brightness compensation
// ========================================================================

// Apply brightness (0..100 percent) to an 8-bit channel using square-root
// compensation so low brightness remains perceptually visible.
inline uint8_t apply_brightness(uint8_t channel, uint8_t brightness_percent)
{
    if (channel == 0 || brightness_percent == 0) {
        return 0;
    }
    if (brightness_percent >= 100) {
        return channel;
    }
    float factor = std::sqrt(static_cast<float>(brightness_percent) / 100.0f);
    int out = static_cast<int>(static_cast<float>(channel) * factor + 0.5f);
    return static_cast<uint8_t>(clamp(out, 0, 255));
}

// Apply brightness to all channels of an RGB state, preserving hue.
inline LightState apply_brightness_to_state(const LightState &state,
                                            uint8_t brightness_percent)
{
    LightState out = state;
    out.brightness = brightness_percent;
    if (!state.power || brightness_percent == 0) {
        out.power = false;
        out.red = 0;
        out.green = 0;
        out.blue = 0;
        out.white = 0;
        return out;
    }
    out.red = apply_brightness(state.red, brightness_percent);
    out.green = apply_brightness(state.green, brightness_percent);
    out.blue = apply_brightness(state.blue, brightness_percent);
    out.white = apply_brightness(state.white, brightness_percent);
    return out;
}

// ========================================================================
// Calibration / limits
// ========================================================================

struct ChannelCalibration {
    float red = 1.0f;
    float green = 1.0f;
    float blue = 1.0f;
    float white = 1.0f;
};

// Apply calibration to an RGBW state, clamping to 255.
inline Rgbw8 apply_calibration(Rgbw8 color, const ChannelCalibration &cal)
{
    color.r = static_cast<uint8_t>(clamp(
        static_cast<int>(static_cast<float>(color.r) * cal.red + 0.5f), 0, 255));
    color.g = static_cast<uint8_t>(clamp(
        static_cast<int>(static_cast<float>(color.g) * cal.green + 0.5f), 0, 255));
    color.b = static_cast<uint8_t>(clamp(
        static_cast<int>(static_cast<float>(color.b) * cal.blue + 0.5f), 0, 255));
    color.w = static_cast<uint8_t>(clamp(
        static_cast<int>(static_cast<float>(color.w) * cal.white + 0.5f), 0, 255));
    return color;
}

// ========================================================================
// Type-aware validation
// ========================================================================

// Returns true when all set properties are valid for the type.
inline bool validate_state_for_type(const LightState &state, const char *type)
{
    bool supports_rgb = false;
    bool supports_white = false;
    bool supports_cct = false;

    if (type == nullptr) return false;

    if (std::strcmp(type, "switch") == 0) {
    } else if (std::strcmp(type, "dimmer") == 0) {
    } else if (std::strcmp(type, "rgb") == 0) {
        supports_rgb = true;
    } else if (std::strcmp(type, "rgbw") == 0) {
        supports_rgb = true;
        supports_white = true;
    } else if (std::strcmp(type, "cct") == 0) {
        supports_cct = true;
    } else if (std::strcmp(type, "addressable_rgb") == 0) {
        supports_rgb = true;
    } else if (std::strcmp(type, "addressable_rgbw") == 0) {
        supports_rgb = true;
        supports_white = true;
    } else {
        supports_rgb = false;
    }

    if (!supports_rgb) {
        if (state.red != 0 || state.green != 0 || state.blue != 0) {
            return false;
        }
    }
    if (!supports_white && state.white != 0) {
        return false;
    }
    if (!supports_cct && state.color_temperature != 0) {
        return false;
    }
    return true;
}

// Normalize a LightState: clamp all channels to valid ranges, zero out
// unsupported channels for the given type, apply calibration.
inline LightState normalize_state(const LightState &state, const char *type,
                                  const ChannelCalibration &cal)
{
    LightState out = state;

    out.brightness = clamp(state.brightness, uint8_t(0), uint8_t(100));

    bool supports_rgb = false;
    bool supports_white = false;

    if (type != nullptr) {
        if (std::strcmp(type, "rgb") == 0 ||
            std::strcmp(type, "rgbw") == 0 ||
            std::strcmp(type, "addressable_rgb") == 0 ||
            std::strcmp(type, "addressable_rgbw") == 0) {
            supports_rgb = true;
        }
        if (std::strcmp(type, "rgbw") == 0 ||
            std::strcmp(type, "addressable_rgbw") == 0) {
            supports_white = true;
        }
    }

    if (!supports_rgb) {
        out.red = 0;
        out.green = 0;
        out.blue = 0;
    }
    if (!supports_white) {
        out.white = 0;
    }
    if (type != nullptr && std::strcmp(type, "cct") != 0) {
        out.color_temperature = 0;
    }

    // Apply calibration to the color channels, then clamp.
    Rgbw8 rgbw = apply_calibration(
        {out.red, out.green, out.blue, out.white}, cal);
    out.red = rgbw.r;
    out.green = rgbw.g;
    out.blue = rgbw.b;
    out.white = rgbw.w;

    return out;
}

} // namespace lumen
