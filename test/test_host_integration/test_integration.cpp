// Host tests for MQTT/HA/TCP/UDP protocol helpers (SPEC §13–14).
#include <string>
#include <vector>

#include <unity.h>

#include "core/cJSON.h"
#include "lumen/networking/integration_protocol.h"

using namespace lumen;
using namespace lumen::integration;

extern "C" void setUp(void) {}
extern "C" void tearDown(void) {}

static Channel make(LightType t)
{
    Channel c;
    c.id = "living_main";
    c.name = "Living Main";
    c.type = t;
    return c;
}

static void test_ha_command_parsing(void)
{
    LightState cur{};
    cur.power = true;
    cJSON *o = cJSON_Parse(R"({"state":"ON","brightness":40,"color":{"r":255,"g":10,"b":0},"transition":1.5})");
    LightCommand cmd;
    std::string err;
    TEST_ASSERT_TRUE_MESSAGE(command_from_ha(o, cur, cmd, err), err.c_str());
    cJSON_Delete(o);
    TEST_ASSERT_EQUAL(kFieldPower | kFieldBrightness | kFieldColor, cmd.fields);
    TEST_ASSERT_EQUAL_UINT8(40, cmd.state.brightness);
    TEST_ASSERT_EQUAL_UINT32(1500, cmd.transition_ms);

    o = cJSON_Parse(R"({"state":"TOGGLE"})");
    TEST_ASSERT_TRUE(command_from_ha(o, cur, cmd, err));
    cJSON_Delete(o);
    TEST_ASSERT_FALSE(cmd.state.power);

    o = cJSON_Parse(R"({"color_temp":250})"); // mireds -> 4000 K
    TEST_ASSERT_TRUE(command_from_ha(o, cur, cmd, err));
    cJSON_Delete(o);
    TEST_ASSERT_EQUAL_UINT16(4000, cmd.state.color_temperature);

    // With color_temp_kelvin:true (our discovery) HA sends "color_temp" in K.
    o = cJSON_Parse(R"({"color_temp":6500})");
    TEST_ASSERT_TRUE(command_from_ha(o, cur, cmd, err));
    cJSON_Delete(o);
    TEST_ASSERT_EQUAL_UINT16(6500, cmd.state.color_temperature);

    o = cJSON_Parse(R"({"effect":"rainbow"})");
    TEST_ASSERT_TRUE_MESSAGE(command_from_ha(o, cur, cmd, err), err.c_str());
    cJSON_Delete(o);
    TEST_ASSERT_EQUAL_STRING("rainbow", cmd.effect.c_str());
    o = cJSON_Parse(R"({"effect":"none"})"); // HA "none" = stop the effect
    TEST_ASSERT_TRUE(command_from_ha(o, cur, cmd, err));
    cJSON_Delete(o);
    TEST_ASSERT_EQUAL_STRING("stop", cmd.effect.c_str());

    const char *bad[] = {R"({"state":"MAYBE"})", R"({"brightness":101})", R"({"color":{"r":300,"g":0,"b":0}})", R"({})", R"([1,2])",
                         R"({"effect":"explode"})"};
    for (const char *b : bad) {
        o = cJSON_Parse(b);
        LightCommand c2;
        TEST_ASSERT_FALSE_MESSAGE(command_from_ha(o, cur, c2, err), b);
        cJSON_Delete(o);
    }
}

static void test_ha_state_and_discovery_per_type(void)
{
    DiscoveryContext ctx;
    ctx.controller_id = "lumen-0798";
    ctx.device_name = device_display_name("lumen-0798");
    TEST_ASSERT_EQUAL_STRING("Lumen 0798", ctx.device_name.c_str());
    TEST_ASSERT_EQUAL_STRING("lumen_0798_living_main", entity_slug("lumen-0798_living_main").c_str());
    TEST_ASSERT_EQUAL_STRING("a_b_c", entity_slug("-A--b..C-").c_str());
    ctx.model = "KinCony KC868-AP";
    ctx.sw_version = "0.1.0";
    Channel cct = make(LightType::kCct);
    cct.limits.color_temperature_min = 2700;
    cct.limits.color_temperature_max = 6500;
    cJSON *cfg = discovery_config(ctx, cct, {"breathing"});
    TEST_ASSERT_EQUAL_STRING("lumen-0798_living_main", cJSON_GetObjectItem(cfg, "unique_id")->valuestring);
    TEST_ASSERT_EQUAL_STRING("lighting/lumen-0798/light/living_main/set", cJSON_GetObjectItem(cfg, "command_topic")->valuestring);
    TEST_ASSERT_EQUAL_STRING("light.lumen_0798_living_main", cJSON_GetObjectItem(cfg, "default_entity_id")->valuestring);
    TEST_ASSERT_NULL(cJSON_GetObjectItem(cfg, "object_id")); // ignored by current HA
    {
        Channel warm = cct;
        warm.requested.power = true;
        warm.requested.color_temperature = 3000;
        cJSON *st = state_to_ha(warm, "");
        // HA reads "color_temp" (kelvin, because color_temp_kelvin:true).
        TEST_ASSERT_EQUAL(3000, cJSON_GetObjectItem(st, "color_temp")->valueint);
        TEST_ASSERT_NULL(cJSON_GetObjectItem(st, "color_temp_kelvin"));
        cJSON_Delete(st);
    }
    TEST_ASSERT_EQUAL_STRING("color_temp", cJSON_GetArrayItem(cJSON_GetObjectItem(cfg, "supported_color_modes"), 0)->valuestring);
    TEST_ASSERT_EQUAL(2700, cJSON_GetObjectItem(cfg, "min_kelvin")->valueint);
    TEST_ASSERT_EQUAL(100, cJSON_GetObjectItem(cfg, "brightness_scale")->valueint);
    cJSON_Delete(cfg);
    TEST_ASSERT_EQUAL_STRING("homeassistant/light/lumen-0798/living_main/config", discovery_topic(ctx, "living_main").c_str());

    Channel sw = make(LightType::kSwitch);
    cfg = discovery_config(ctx, sw, {"breathing"});
    TEST_ASSERT_NULL(cJSON_GetObjectItem(cfg, "brightness")); // never advertise unsupported features
    TEST_ASSERT_NULL(cJSON_GetObjectItem(cfg, "effect_list"));
    cJSON_Delete(cfg);

    Channel rgb = make(LightType::kRgb);
    rgb.requested.power = true;
    rgb.requested.brightness = 70;
    rgb.requested.red = 9;
    cJSON *st = state_to_ha(rgb, "");
    TEST_ASSERT_EQUAL_STRING("ON", cJSON_GetObjectItem(st, "state")->valuestring);
    TEST_ASSERT_EQUAL_STRING("rgb", cJSON_GetObjectItem(st, "color_mode")->valuestring);
    TEST_ASSERT_EQUAL(9, cJSON_GetObjectItem(cJSON_GetObjectItem(st, "color"), "r")->valueint);
    cJSON_Delete(st);
    TEST_ASSERT_EQUAL_STRING("a_b_c", topic_safe("A/B+C").c_str());
}

static void test_line_framer_handles_partial_and_oversized(void)
{
    LineFramer f(32);
    std::vector<std::string> lines;
    TEST_ASSERT_TRUE(f.feed("{\"a\":1}\n{\"b\"", 12, lines));
    TEST_ASSERT_EQUAL(1, static_cast<int>(lines.size()));
    TEST_ASSERT_TRUE(f.feed(":2}\r\n\n", 6, lines)); // completes, CRLF, blank line ignored
    TEST_ASSERT_EQUAL(2, static_cast<int>(lines.size()));
    TEST_ASSERT_EQUAL_STRING("{\"b\":2}", lines[1].c_str());
    std::string big(40, 'x');
    TEST_ASSERT_FALSE(f.feed(big.data(), big.size(), lines));
}

static void test_hmac_known_vector_and_replay_guard(void)
{
    // RFC 4231 test case 2.
    const char *key = "Jefe";
    const char *msg = "what do ya want for nothing?";
    uint8_t out[32];
    hmac_sha256(reinterpret_cast<const uint8_t *>(key), 4, reinterpret_cast<const uint8_t *>(msg), 28, out);
    TEST_ASSERT_EQUAL_STRING("5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843", hex(out, 32).c_str());

    ReplayGuard g;
    const int64_t now = 1800000000;
    TEST_ASSERT_TRUE(g.accept("pc", 1, now, now));
    TEST_ASSERT_FALSE(g.accept("pc", 1, now, now));          // replay
    TEST_ASSERT_FALSE(g.accept("pc", 0, now, now));          // older seq
    TEST_ASSERT_TRUE(g.accept("pc", 2, now + 5, now));
    TEST_ASSERT_FALSE(g.accept("pc", 3, now - 120, now));    // outside window
    TEST_ASSERT_TRUE(g.accept("phone", 1, now, now));        // independent sender
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_ha_command_parsing);
    RUN_TEST(test_ha_state_and_discovery_per_type);
    RUN_TEST(test_line_framer_handles_partial_and_oversized);
    RUN_TEST(test_hmac_known_vector_and_replay_guard);
    return UNITY_END();
}
