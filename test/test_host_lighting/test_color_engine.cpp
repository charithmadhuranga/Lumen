// Host tests for the color engine (SPEC §8.4) — pure functions with golden
// values. No I/O, no heap, fully host-testable.
#include <cstring>

#include <unity.h>

#include "lumen/lighting/color_engine.h"
#include "lumen/lighting/transition.h"

using lumen::LightState;
using lumen::Rgb8;
using lumen::Rgbw8;
using lumen::HsvState;
using lumen::ChannelCalibration;
using lumen::Transition;
using lumen::LightCommand;
using lumen::ApplyResult;
using lumen::ease_linear;
using lumen::ease_smooth;
using lumen::ease_inout_cubic;
using lumen::transition_tick;
using lumen::apply_command;
using lumen::apply_gamma;
using lumen::apply_brightness;
using lumen::apply_brightness_to_state;
using lumen::apply_calibration;
using lumen::gamma8_table;
using lumen::hsv_to_rgb;
using lumen::kelvin_to_rgb;
using lumen::normalize_state;
using lumen::rgb_to_hsv;
using lumen::rgb_to_kelvin;
using lumen::rgb_to_rgbw;
using lumen::rgbw_to_rgb;
using lumen::validate_state_for_type;

namespace
{

extern "C" void setUp(void) {}
extern "C" void tearDown(void) {}

} // namespace

// ---------------------------------------------------------------------------
// Gamma table
// ---------------------------------------------------------------------------
static void test_gamma_table_bounds(void)
{
    const uint8_t *g = gamma8_table();
    TEST_ASSERT_EQUAL(0, g[0]);
    TEST_ASSERT_EQUAL(255, g[255]);
    TEST_ASSERT_TRUE(g[1] <= g[2]); // monotonic
    TEST_ASSERT_TRUE(g[254] <= g[255]);
}

static void test_apply_gamma(void)
{
    TEST_ASSERT_EQUAL(0, apply_gamma(0));
    TEST_ASSERT_EQUAL(255, apply_gamma(255));
    // Very low values round to 0 under gamma correction
    TEST_ASSERT_EQUAL(0, apply_gamma(1));
    TEST_ASSERT_EQUAL(1, apply_gamma(15));
}

// ---------------------------------------------------------------------------
// RGB <-> HSV
// ---------------------------------------------------------------------------
static void test_rgb_to_hsv_grayscale(void)
{
    HsvState h = rgb_to_hsv(0, 0, 0);
    TEST_ASSERT_EQUAL(0, h.hue);
    TEST_ASSERT_EQUAL(0, h.saturation);
    TEST_ASSERT_EQUAL(0, h.value);

    h = rgb_to_hsv(128, 128, 128);
    TEST_ASSERT_EQUAL(0, h.hue);
    TEST_ASSERT_EQUAL(0, h.saturation);
    TEST_ASSERT_EQUAL(50, h.value);

    h = rgb_to_hsv(255, 255, 255);
    TEST_ASSERT_EQUAL(0, h.hue);
    TEST_ASSERT_EQUAL(0, h.saturation);
    TEST_ASSERT_EQUAL(100, h.value);
}

static void test_rgb_to_hsv_red(void)
{
    HsvState h = rgb_to_hsv(255, 0, 0);
    TEST_ASSERT_EQUAL(0, h.hue);
    TEST_ASSERT_EQUAL(100, h.saturation);
    TEST_ASSERT_EQUAL(100, h.value);
}

static void test_rgb_to_hsv_green(void)
{
    HsvState h = rgb_to_hsv(0, 255, 0);
    TEST_ASSERT_EQUAL(120, h.hue);
    TEST_ASSERT_EQUAL(100, h.saturation);
    TEST_ASSERT_EQUAL(100, h.value);
}

static void test_rgb_to_hsv_blue(void)
{
    HsvState h = rgb_to_hsv(0, 0, 255);
    TEST_ASSERT_EQUAL(240, h.hue);
    TEST_ASSERT_EQUAL(100, h.saturation);
    TEST_ASSERT_EQUAL(100, h.value);
}

static void test_hsv_to_rgb_roundtrip(void)
{
    // Pure red
    HsvState h{0, 100, 100};
    Rgb8 rgb = hsv_to_rgb(h);
    TEST_ASSERT_EQUAL(255, rgb.r);
    TEST_ASSERT_EQUAL(0, rgb.g);
    TEST_ASSERT_EQUAL(0, rgb.b);

    // Pure green
    h = {120, 100, 100};
    rgb = hsv_to_rgb(h);
    TEST_ASSERT_EQUAL(0, rgb.r);
    TEST_ASSERT_EQUAL(255, rgb.g);
    TEST_ASSERT_EQUAL(0, rgb.b);

    // Pure blue
    h = {240, 100, 100};
    rgb = hsv_to_rgb(h);
    TEST_ASSERT_EQUAL(0, rgb.r);
    TEST_ASSERT_EQUAL(0, rgb.g);
    TEST_ASSERT_EQUAL(255, rgb.b);

    // White (saturation 0)
    h = {0, 0, 50};
    rgb = hsv_to_rgb(h);
    TEST_ASSERT_EQUAL(128, rgb.r);
    TEST_ASSERT_EQUAL(128, rgb.g);
    TEST_ASSERT_EQUAL(128, rgb.b);
}

// ---------------------------------------------------------------------------
// RGBW
// ---------------------------------------------------------------------------
static void test_rgb_to_rgbw(void)
{
    // Pure red -> white=0, red=255
    Rgbw8 rgbw = rgb_to_rgbw(255, 0, 0);
    TEST_ASSERT_EQUAL(255, rgbw.r);
    TEST_ASSERT_EQUAL(0, rgbw.g);
    TEST_ASSERT_EQUAL(0, rgbw.b);
    TEST_ASSERT_EQUAL(0, rgbw.w);

    // Gray 100 -> white=100, colors=0
    rgbw = rgb_to_rgbw(100, 100, 100);
    TEST_ASSERT_EQUAL(0, rgbw.r);
    TEST_ASSERT_EQUAL(0, rgbw.g);
    TEST_ASSERT_EQUAL(0, rgbw.b);
    TEST_ASSERT_EQUAL(100, rgbw.w);
}

static void test_rgbw_to_rgb(void)
{
    // White channel only
    Rgb8 rgb = rgbw_to_rgb(0, 0, 0, 100);
    TEST_ASSERT_EQUAL(100, rgb.r);
    TEST_ASSERT_EQUAL(100, rgb.g);
    TEST_ASSERT_EQUAL(100, rgb.b);

    // Red + white clamps
    rgb = rgbw_to_rgb(200, 0, 0, 100);
    TEST_ASSERT_EQUAL(255, rgb.r); // clamped
    TEST_ASSERT_EQUAL(100, rgb.g);
    TEST_ASSERT_EQUAL(100, rgb.b);
}

// ---------------------------------------------------------------------------
// CCT
// ---------------------------------------------------------------------------
static void test_kelvin_to_rgb_warm(void)
{
    Rgb8 rgb = kelvin_to_rgb(1000);
    TEST_ASSERT_TRUE_MESSAGE(rgb.r > 100, "warm should have high red");
    TEST_ASSERT_TRUE_MESSAGE(rgb.g < 100, "warm should have low green");
    TEST_ASSERT_EQUAL(0, rgb.b);     // no blue at 1000K
}

static void test_kelvin_to_rgb_cool(void)
{
    Rgb8 rgb = kelvin_to_rgb(10000);
    TEST_ASSERT_TRUE_MESSAGE(rgb.r < 255, "cool should have reduced red");
    TEST_ASSERT_EQUAL(255, rgb.b);   // cool = lots of blue
}

static void test_kelvin_to_rgb_clamp(void)
{
    // Below minimum should clamp to 1000K behavior
    Rgb8 low = kelvin_to_rgb(100);
    Rgb8 min = kelvin_to_rgb(1000);
    TEST_ASSERT_EQUAL(low.r, min.r);
    TEST_ASSERT_EQUAL(low.g, min.g);
    TEST_ASSERT_EQUAL(low.b, min.b);

    // Above maximum should clamp to 20000K behavior
    Rgb8 high = kelvin_to_rgb(50000);
    Rgb8 max = kelvin_to_rgb(20000);
    TEST_ASSERT_EQUAL(high.r, max.r);
    TEST_ASSERT_EQUAL(high.g, max.g);
    TEST_ASSERT_EQUAL(high.b, max.b);
}

static void test_rgb_to_kelvin_saturated_returns_zero(void)
{
    // Saturated red is far from blackbody locus
    uint16_t k = rgb_to_kelvin(255, 0, 0);
    TEST_ASSERT_EQUAL(0, k);
}

// ---------------------------------------------------------------------------
// Brightness compensation
// ---------------------------------------------------------------------------
static void test_apply_brightness(void)
{
    TEST_ASSERT_EQUAL(0, apply_brightness(100, 0));
    TEST_ASSERT_EQUAL(100, apply_brightness(100, 100));
    TEST_ASSERT_EQUAL(0, apply_brightness(0, 50));

    // Square-root compensation: 50% brightness of 100 = 100*sqrt(0.5) ~ 71
    uint8_t v = apply_brightness(100, 50);
    TEST_ASSERT_TRUE(v >= 60 && v <= 80);
}

static void test_apply_brightness_to_state(void)
{
    LightState s{true, 50, 200, 100, 50, 30, 2700};
    LightState out = apply_brightness_to_state(s, 0);
    TEST_ASSERT_FALSE(out.power);
    TEST_ASSERT_EQUAL(0, out.red);
    TEST_ASSERT_EQUAL(0, out.green);
    TEST_ASSERT_EQUAL(0, out.blue);
    TEST_ASSERT_EQUAL(0, out.white);

    out = apply_brightness_to_state(s, 100);
    TEST_ASSERT_TRUE(out.power);
    TEST_ASSERT_EQUAL(100, out.brightness);
}

// ---------------------------------------------------------------------------
// Calibration
// ---------------------------------------------------------------------------
static void test_apply_calibration(void)
{
    Rgbw8 in{100, 150, 200, 50};
    ChannelCalibration cal{0.5f, 2.0f, 1.0f, 1.0f};
    Rgbw8 out = apply_calibration(in, cal);

    TEST_ASSERT_EQUAL(50, out.r);   // 100 * 0.5
    TEST_ASSERT_EQUAL(255, out.g);  // 150 * 2.0 = 300, clamped to 255
    TEST_ASSERT_EQUAL(200, out.b);  // 200 * 1.0
    TEST_ASSERT_EQUAL(50, out.w);   // 50 * 1.0
}

// ---------------------------------------------------------------------------
// Type-aware validation
// ---------------------------------------------------------------------------
static void test_validate_switch_rejects_color(void)
{
    LightState s{true, 50, 200, 0, 0, 0, 0}; // red set on switch
    TEST_ASSERT_FALSE(validate_state_for_type(s, "switch"));
}

static void test_validate_dimmer_rejects_color(void)
{
    LightState s{true, 50, 0, 100, 0, 0, 0}; // green set on dimmer
    TEST_ASSERT_FALSE(validate_state_for_type(s, "dimmer"));
}

static void test_validate_rgb_accepts_color(void)
{
    LightState s{true, 50, 200, 100, 50, 0, 0};
    TEST_ASSERT_TRUE(validate_state_for_type(s, "rgb"));
}

static void test_validate_rgbw_accepts_white(void)
{
    LightState s{true, 50, 100, 100, 100, 100, 0};
    TEST_ASSERT_TRUE(validate_state_for_type(s, "rgbw"));
}

static void test_validate_rgbw_rejects_cct(void)
{
    LightState s{true, 50, 0, 0, 0, 0, 3000};
    TEST_ASSERT_FALSE(validate_state_for_type(s, "rgbw"));
}

static void test_validate_cct_accepts_cct(void)
{
    LightState s{true, 50, 0, 0, 0, 0, 3000};
    TEST_ASSERT_TRUE(validate_state_for_type(s, "cct"));
}

static void test_validate_cct_rejects_rgb(void)
{
    LightState s{true, 50, 100, 0, 0, 0, 0};
    TEST_ASSERT_FALSE(validate_state_for_type(s, "cct"));
}

static void test_validate_switch_accepts_brightness(void)
{
    LightState s{true, 50, 0, 0, 0, 0, 0};
    TEST_ASSERT_TRUE(validate_state_for_type(s, "switch"));
}

// ---------------------------------------------------------------------------
// Normalize
// ---------------------------------------------------------------------------
static void test_normalize_clamps_brightness(void)
{
    LightState s{true, 150, 100, 50, 0, 0, 2700}; // brightness > 100
    LightState out = normalize_state(s, "dimmer", ChannelCalibration{});
    TEST_ASSERT_EQUAL(100, out.brightness);
}

static void test_normalize_zeros_unsupported_rgb_on_dimmer(void)
{
    LightState s{true, 50, 100, 50, 0, 0, 0}; // dimmer with RGB
    LightState out = normalize_state(s, "dimmer", ChannelCalibration{});
    TEST_ASSERT_EQUAL(0, out.red);
    TEST_ASSERT_EQUAL(0, out.green);
    TEST_ASSERT_EQUAL(0, out.blue);
    TEST_ASSERT_EQUAL(50, out.brightness);
}

static void test_normalize_cct_zeros_rgb(void)
{
    LightState s{true, 50, 100, 50, 0, 0, 3000};
    LightState out = normalize_state(s, "cct", ChannelCalibration{});
    TEST_ASSERT_EQUAL(0, out.red);
    TEST_ASSERT_EQUAL(0, out.green);
    TEST_ASSERT_EQUAL(0, out.blue);
    TEST_ASSERT_EQUAL(3000, out.color_temperature);
}

// ========================================================================
// Transition engine tests
// ========================================================================
static void test_ease_linear(void)
{
    TEST_ASSERT_EQUAL(0, ease_linear(0));
    TEST_ASSERT_EQUAL(500, ease_linear(500));
    TEST_ASSERT_EQUAL(1000, ease_linear(1000));
    TEST_ASSERT_EQUAL(250, ease_linear(250));
}

static void test_ease_smooth(void)
{
    TEST_ASSERT_EQUAL(0, ease_smooth(0));
    TEST_ASSERT_EQUAL(1000, ease_smooth(1000));
    TEST_ASSERT_EQUAL(500, ease_smooth(500));
    TEST_ASSERT_TRUE(ease_smooth(250) < 250);
    TEST_ASSERT_TRUE(ease_smooth(750) > 750);
}

static void test_ease_inout_cubic(void)
{
    TEST_ASSERT_EQUAL(0, ease_inout_cubic(0));
    TEST_ASSERT_EQUAL(1000, ease_inout_cubic(1000));
    TEST_ASSERT_EQUAL(500, ease_inout_cubic(500));
}

static void test_transition_inactive_returns_false(void)
{
    Transition t{};
    LightState out{};
    TEST_ASSERT_FALSE(transition_tick(t, 1000, out));
}

static void test_transition_instant_complete(void)
{
    Transition t{};
    t.active = true;
    t.start_ms = 0;
    t.duration_ms = 0;
    t.start_state = {true, 0, 0, 0, 0, 0, 0};
    t.end_state = {true, 100, 255, 0, 0, 0, 0};

    LightState out{};
    bool active = transition_tick(t, 500, out);
    TEST_ASSERT_FALSE(active);
    TEST_ASSERT_FALSE(t.active);
    TEST_ASSERT_EQUAL(100, out.brightness);
    TEST_ASSERT_EQUAL(255, out.red);
}

static void test_transition_linear_interpolation(void)
{
    Transition t{};
    t.active = true;
    t.start_ms = 0;
    t.duration_ms = 1000;
    t.start_state = {true, 0, 0, 0, 0, 0, 0};
    t.end_state = {true, 100, 255, 0, 0, 0, 0};
    t.ease = ease_linear;

    LightState out{};
    bool active = transition_tick(t, 500, out);
    TEST_ASSERT_TRUE(active);
    TEST_ASSERT_EQUAL(50, out.brightness);
    TEST_ASSERT_EQUAL(127, out.red);

    active = transition_tick(t, 1000, out);
    TEST_ASSERT_FALSE(active);
    TEST_ASSERT_EQUAL(100, out.brightness);
    TEST_ASSERT_EQUAL(255, out.red);
}

static void test_transition_smooth_easing(void)
{
    Transition t{};
    t.active = true;
    t.start_ms = 0;
    t.duration_ms = 1000;
    t.start_state = {true, 0, 0, 0, 0, 0, 0};
    t.end_state = {true, 100, 0, 0, 0, 0, 0};
    t.ease = ease_smooth;

    LightState out{};
    transition_tick(t, 500, out);
    TEST_ASSERT_EQUAL(50, out.brightness);

    transition_tick(t, 250, out);
    TEST_ASSERT_TRUE(out.brightness < 25);
}

static void test_transition_power_interpolation(void)
{
    Transition t{};
    t.active = true;
    t.start_ms = 0;
    t.duration_ms = 1000;
    t.start_state = {false, 0, 0, 0, 0, 0, 0};
    t.end_state = {true, 100, 255, 0, 0, 0, 0};
    t.ease = ease_linear;

    LightState out{};
    transition_tick(t, 499, out);
    TEST_ASSERT_FALSE(out.power);

    transition_tick(t, 500, out);
    TEST_ASSERT_TRUE(out.power);
}

static void test_transition_cancel_on_new_command(void)
{
    Transition t{};
    t.active = true;
    t.start_ms = 0;
    t.duration_ms = 5000;
    t.start_state = {true, 0, 0, 0, 0, 0, 0};
    t.end_state = {true, 100, 0, 0, 0, 0, 0};

    LightState current = {true, 10, 0, 0, 0, 0, 0};
    LightCommand cmd{};
    cmd.channel_id = "light1";
    cmd.state = {true, 50, 0, 0, 0, 0, 0};
    cmd.transition_ms = 2000;

    ApplyResult result = apply_command(t, cmd, current, 1000);
    TEST_ASSERT_TRUE(result.state_changed);
    TEST_ASSERT_TRUE(t.active);
    TEST_ASSERT_EQUAL(2000, t.duration_ms);
    TEST_ASSERT_EQUAL(1000, t.start_ms);
}

static void test_apply_command_instant(void)
{
    Transition t{};
    LightCommand cmd{};
    cmd.channel_id = "light1";
    cmd.state = {true, 75, 200, 100, 50, 0, 0};
    cmd.transition_ms = 0;

    LightState current = {false, 0, 0, 0, 0, 0, 0};
    ApplyResult result = apply_command(t, cmd, current, 500);

    TEST_ASSERT_TRUE(result.state_changed);
    TEST_ASSERT_FALSE(t.active);
    TEST_ASSERT_EQUAL(true, result.new_state.power);
    TEST_ASSERT_EQUAL(75, result.new_state.brightness);
    TEST_ASSERT_EQUAL(200, result.new_state.red);
    TEST_ASSERT_EQUAL(100, result.new_state.green);
    TEST_ASSERT_EQUAL(50, result.new_state.blue);
}

static void test_apply_command_transition(void)
{
    Transition t{};
    LightCommand cmd{};
    cmd.channel_id = "light1";
    cmd.state = {true, 100, 255, 0, 0, 0, 0};
    cmd.transition_ms = 1000;

    LightState current = {true, 0, 0, 0, 0, 0, 0};
    ApplyResult result = apply_command(t, cmd, current, 0);

    TEST_ASSERT_TRUE(result.state_changed);
    TEST_ASSERT_TRUE(t.active);
    TEST_ASSERT_EQUAL(1000, t.duration_ms);
    TEST_ASSERT_EQUAL(0, result.new_state.brightness);
    TEST_ASSERT_EQUAL(0, result.new_state.red);
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    UNITY_BEGIN();
    // Gamma
    RUN_TEST(test_gamma_table_bounds);
    RUN_TEST(test_apply_gamma);
    // RGB <-> HSV
    RUN_TEST(test_rgb_to_hsv_grayscale);
    RUN_TEST(test_rgb_to_hsv_red);
    RUN_TEST(test_rgb_to_hsv_green);
    RUN_TEST(test_rgb_to_hsv_blue);
    RUN_TEST(test_hsv_to_rgb_roundtrip);
    // RGBW
    RUN_TEST(test_rgb_to_rgbw);
    RUN_TEST(test_rgbw_to_rgb);
    // CCT
    RUN_TEST(test_kelvin_to_rgb_warm);
    RUN_TEST(test_kelvin_to_rgb_cool);
    RUN_TEST(test_kelvin_to_rgb_clamp);
    RUN_TEST(test_rgb_to_kelvin_saturated_returns_zero);
    // Brightness
    RUN_TEST(test_apply_brightness);
    RUN_TEST(test_apply_brightness_to_state);
    // Calibration
    RUN_TEST(test_apply_calibration);
    // Validation
    RUN_TEST(test_validate_switch_rejects_color);
    RUN_TEST(test_validate_dimmer_rejects_color);
    RUN_TEST(test_validate_rgb_accepts_color);
    RUN_TEST(test_validate_rgbw_accepts_white);
    RUN_TEST(test_validate_rgbw_rejects_cct);
    RUN_TEST(test_validate_cct_accepts_cct);
    RUN_TEST(test_validate_cct_rejects_rgb);
    RUN_TEST(test_validate_switch_accepts_brightness);
    // Normalize
    RUN_TEST(test_normalize_clamps_brightness);
    RUN_TEST(test_normalize_zeros_unsupported_rgb_on_dimmer);
    RUN_TEST(test_normalize_cct_zeros_rgb);
    // Transition engine
    RUN_TEST(test_ease_linear);
    RUN_TEST(test_ease_smooth);
    RUN_TEST(test_ease_inout_cubic);
    RUN_TEST(test_transition_inactive_returns_false);
    RUN_TEST(test_transition_instant_complete);
    RUN_TEST(test_transition_linear_interpolation);
    RUN_TEST(test_transition_smooth_easing);
    RUN_TEST(test_transition_power_interpolation);
    RUN_TEST(test_transition_cancel_on_new_command);
    RUN_TEST(test_apply_command_instant);
    RUN_TEST(test_apply_command_transition);
    return UNITY_END();
}
