// Host tests for the lighting engine end to end: commands -> transitions/
// effects -> driver adapters -> (mock) hardware. Unlike the registry tests,
// these assert what actually reaches the outputs.
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

#include <unity.h>

#include "core/cJSON.h"
#include "lumen/core/config_manager.hpp"
#include "lumen/core/event_bus.hpp"
#include "lumen/core/storage.hpp"
#include "lumen/hardware/board_profile_loader.h"
#include "lumen/hardware/board_service.h"
#include "lumen/hardware/hardware_manager.h"
#include "lumen/hardware/hal/i_relay_driver.h"
#include "lumen/lighting/group_manager.h"
#include "lumen/lighting/light_store.h"
#include "lumen/lighting/lighting_task.h"
#include "lumen/lighting/room_manager.h"
#include "lumen/lighting/scene_manager.h"

#include "mock_i2c_bus.h"

using namespace lumen;

namespace
{

// 16-channel PWM device that records every channel write and flush.
class MockPwm final : public IPwmDriver
{
  public:
    esp_err_t initialize() override { return ESP_OK; }
    esp_err_t shutdown() override { return ESP_OK; }
    DriverState state() const override { return DriverState::kInitialized; }
    const char *name() const override { return "mock_pwm"; }
    DriverType driver_type() const override { return DriverType::kPwm; }
    esp_err_t set_duty(PwmDuty d) override { return set_channel_duty(0, d); }
    esp_err_t get_duty(PwmDuty &d) override { d = duty[0]; return ESP_OK; }
    esp_err_t set_frequency(uint32_t) override { return ESP_OK; }
    esp_err_t get_frequency(uint32_t &f) override { f = 1000; return ESP_OK; }
    esp_err_t set_resolution(uint8_t) override { return ESP_OK; }
    esp_err_t get_resolution(uint8_t &r) override { r = 12; return ESP_OK; }
    esp_err_t set_inverted(bool) override { return ESP_OK; }
    esp_err_t get_inverted(bool &i) override { i = false; return ESP_OK; }
    esp_err_t set_channel_duty(uint8_t ch, PwmDuty d) override
    {
        if (ch >= 16) return ESP_ERR_INVALID_ARG;
        duty[ch] = d;
        writes++;
        return ESP_OK;
    }
    esp_err_t flush() override
    {
        flushes++;
        return ESP_OK;
    }
    PwmDuty duty[16] = {};
    int writes = 0;
    int flushes = 0;
};

class MockRelay final : public IRelayDriver
{
  public:
    esp_err_t initialize() override { return ESP_OK; }
    esp_err_t shutdown() override { return ESP_OK; }
    DriverState state() const override { return DriverState::kInitialized; }
    const char *name() const override { return "mock_relay"; }
    DriverType driver_type() const override { return DriverType::kRelay; }
    esp_err_t set_value(uint32_t v) override { on = v != 0; return ESP_OK; }
    esp_err_t get_value(uint32_t &v) override { v = on; return ESP_OK; }
    esp_err_t set_safe_state(uint32_t, bool) override { return ESP_OK; }
    esp_err_t get_safe_state(uint32_t &v, bool &a) override { v = 0; a = true; return ESP_OK; }
    esp_err_t set_polarity(RelayPolarity) override { return ESP_OK; }
    esp_err_t get_polarity(RelayPolarity &p) override { p = RelayPolarity::kActiveHigh; return ESP_OK; }
    esp_err_t turn_on() override { on = true; return ESP_OK; }
    esp_err_t turn_off() override { on = false; return ESP_OK; }
    bool on = false;
};

class MockHw final : public IHardwareManager
{
  public:
    IDriver *driver(const std::string &id) override
    {
        if (id == "pca") return &pwm;
        if (id == "relay1") return &relay;
        return nullptr;
    }
    esp_err_t flush_outputs() override { return pwm.flush(); }
    MockPwm pwm;
    MockRelay relay;
};

Channel make(const std::string &id, LightType type, const std::string &drv, const std::string &dev,
             int ch)
{
    Channel c;
    c.id = id;
    c.name = id;
    c.type = type;
    c.driver = {drv, dev, ch};
    c.transition_ms = 0;
    return c;
}

void cmd(LightingTask &t, const std::string &id, LightState s, uint8_t fields, uint32_t ms = 0)
{
    LightCommandEnvelope e;
    e.command.channel_id = id;
    e.command.state = s;
    e.command.fields = fields;
    e.command.transition_ms = ms;
    TEST_ASSERT_EQUAL(static_cast<int>(EnqueueResult::kOk), static_cast<int>(t.enqueue(e)));
}

uint16_t pca12(uint8_t percent) // expected 12-bit duty for a brightness
{
    return static_cast<uint16_t>((static_cast<uint32_t>(perceptual_level(percent)) * 4095U + 32767U) / 65535U);
}

struct NoopLock final : ILock
{
    void lock() override {}
    void unlock() override {}
};
struct Clock final : IClock
{
    uint32_t ms() override { return 0; }
};

std::string tmpdir(const char *name)
{
    std::string d = std::string("/tmp/lumen_engine_") + name;
    std::string cmdline = "rm -rf " + d + " && mkdir -p " + d;
    TEST_ASSERT_EQUAL(0, std::system(cmdline.c_str()));
    return d;
}

} // namespace

extern "C" void setUp(void) {}
extern "C" void tearDown(void) {}

static void test_dimmer_command_reaches_hardware(void)
{
    MockHw hw;
    LightingTask t;
    TEST_ASSERT_TRUE(t.register_channel(make("d", LightType::kDimmer, "pca9685", "pca", 3)));
    t.attach_hardware(&hw);
    TEST_ASSERT_TRUE(t.hardware_ok("d"));
    LightState s{};
    s.brightness = 50;
    cmd(t, "d", s, kFieldBrightness); // brightness > 0 implies power on
    t.run_once(0);
    TEST_ASSERT_EQUAL_UINT16(pca12(50), hw.pwm.duty[3]);
    TEST_ASSERT_EQUAL_UINT16(0, hw.pwm.duty[0]); // other channels untouched
    TEST_ASSERT_TRUE(hw.pwm.flushes > 0);
}

static void test_rgb_uses_consecutive_channels_from_offset(void)
{
    MockHw hw;
    LightingTask t;
    t.register_channel(make("rgb", LightType::kRgb, "pca9685", "pca", 8));
    t.attach_hardware(&hw);
    LightState s{};
    s.power = true;
    s.brightness = 100;
    s.red = 255;
    s.green = 0;
    s.blue = 128;
    cmd(t, "rgb", s, kFieldAll);
    t.run_once(0);
    TEST_ASSERT_EQUAL_UINT16(4095, hw.pwm.duty[8]);
    TEST_ASSERT_EQUAL_UINT16(0, hw.pwm.duty[9]);
    TEST_ASSERT_UINT16_WITHIN(20, 2055, hw.pwm.duty[10]);
}

static void test_partial_command_keeps_colour(void)
{
    MockHw hw;
    LightingTask t;
    t.register_channel(make("rgb", LightType::kRgb, "pca9685", "pca", 0));
    t.attach_hardware(&hw);
    LightState s{};
    s.power = true;
    s.brightness = 100;
    s.red = 255;
    cmd(t, "rgb", s, kFieldAll);
    t.run_once(0);
    LightState b{};
    b.brightness = 50;
    cmd(t, "rgb", b, kFieldBrightness);
    t.run_once(20);
    LightState req{};
    t.get_requested("rgb", req);
    TEST_ASSERT_EQUAL_UINT8(255, req.red);
    TEST_ASSERT_EQUAL_UINT8(50, req.brightness);
    TEST_ASSERT_EQUAL_UINT16(pca12(50), hw.pwm.duty[0]);
}

static void test_transition_interpolates_then_snaps(void)
{
    MockHw hw;
    LightingTask t;
    t.register_channel(make("d", LightType::kDimmer, "pca9685", "pca", 0));
    t.attach_hardware(&hw);
    LightState s{};
    s.power = true;
    s.brightness = 100;
    cmd(t, "d", s, kFieldAll, 1000);
    t.run_once(0);
    TEST_ASSERT_TRUE(t.animating("d"));
    t.run_once(500);
    const PwmDuty mid = hw.pwm.duty[0];
    TEST_ASSERT_TRUE(mid > 0 && mid < 4095);
    t.run_once(1000);
    TEST_ASSERT_EQUAL_UINT16(4095, hw.pwm.duty[0]);
    TEST_ASSERT_FALSE(t.animating("d"));
}

static void test_newer_command_replaces_transition(void)
{
    MockHw hw;
    LightingTask t;
    t.register_channel(make("d", LightType::kDimmer, "pca9685", "pca", 0));
    t.attach_hardware(&hw);
    LightState s{};
    s.power = true;
    s.brightness = 100;
    cmd(t, "d", s, kFieldAll, 10000);
    t.run_once(0);
    t.run_once(100);
    LightState off{};
    cmd(t, "d", off, kFieldPower, 0);
    t.run_once(120);
    TEST_ASSERT_EQUAL_UINT16(0, hw.pwm.duty[0]);
    TEST_ASSERT_FALSE(t.animating("d"));
}

static void test_redundant_writes_skipped(void)
{
    MockHw hw;
    LightingTask t;
    t.register_channel(make("d", LightType::kDimmer, "pca9685", "pca", 0));
    t.attach_hardware(&hw);
    t.run_once(0);
    const int writes = hw.pwm.writes;
    for (uint32_t i = 1; i <= 50; ++i) t.run_once(i * 20);
    TEST_ASSERT_EQUAL(writes, hw.pwm.writes);
}

static void test_relay_switch_and_unknown_driver(void)
{
    MockHw hw;
    LightingTask t;
    t.register_channel(make("sw", LightType::kSwitch, "relay", "relay1", 0));
    t.register_channel(make("ghost", LightType::kDimmer, "pca9685", "missing", 0));
    t.attach_hardware(&hw);
    TEST_ASSERT_FALSE(t.hardware_ok("ghost")); // missing device: unavailable, no crash
    LightState s{};
    s.power = true;
    cmd(t, "sw", s, kFieldPower);
    cmd(t, "ghost", s, kFieldPower);
    t.run_once(0);
    TEST_ASSERT_TRUE(hw.relay.on);
    cmd(t, "sw", LightState{}, kFieldPower);
    t.run_once(20);
    TEST_ASSERT_FALSE(hw.relay.on);
}

static void test_effect_runs_and_plain_command_cancels(void)
{
    MockHw hw;
    LightingTask t;
    t.register_channel(make("d", LightType::kDimmer, "pca9685", "pca", 0));
    t.attach_hardware(&hw);
    LightCommandEnvelope e;
    e.command.channel_id = "d";
    e.command.fields = kFieldBrightness;
    e.command.state.brightness = 100;
    e.command.effect = "breathing";
    e.command.effect_speed_ms = 1000;
    t.enqueue(e);
    t.run_once(0);
    TEST_ASSERT_EQUAL_STRING("breathing", t.active_effect("d").c_str());
    std::vector<PwmDuty> seen;
    for (uint32_t ms = 0; ms <= 1000; ms += 100) {
        t.run_once(ms);
        seen.push_back(hw.pwm.duty[0]);
    }
    bool varies = false;
    for (auto v : seen) varies |= v != seen.front();
    TEST_ASSERT_TRUE(varies);
    LightState s{};
    s.brightness = 30;
    cmd(t, "d", s, kFieldBrightness);
    t.run_once(1100);
    TEST_ASSERT_TRUE(t.active_effect("d").empty());
    TEST_ASSERT_EQUAL_UINT16(pca12(30), hw.pwm.duty[0]);
}

static void test_cct_mixes_warm_and_cool(void)
{
    MockHw hw;
    LightingTask t;
    t.register_channel(make("w", LightType::kCct, "pca9685", "pca", 0));
    t.attach_hardware(&hw);
    LightState s{};
    s.power = true;
    s.brightness = 100;
    s.color_temperature = 2700; // warmest -> warm channel only
    cmd(t, "w", s, kFieldAll);
    t.run_once(0);
    TEST_ASSERT_EQUAL_UINT16(4095, hw.pwm.duty[0]);
    TEST_ASSERT_EQUAL_UINT16(0, hw.pwm.duty[1]);
    s.color_temperature = 6500; // coolest -> cool channel only
    cmd(t, "w", s, kFieldCct);
    t.run_once(20);
    TEST_ASSERT_EQUAL_UINT16(0, hw.pwm.duty[0]);
    TEST_ASSERT_EQUAL_UINT16(4095, hw.pwm.duty[1]);
}

static void test_room_group_scene_fan_out_reaches_hardware(void)
{
    const std::string dir = tmpdir("fanout");
    IStorage *storage = create_posix_fs(dir);
    IJson *json = create_cjson_adapter();
    NoopLock lock;
    Clock clock;
    Event pool[16];
    EventBus bus(lock, clock, pool, 16);
    ConfigManager config(*storage, *json, bus);
    for (const char *doc : {"rooms.json", "groups.json", "scenes.json"}) {
        DocConfig c{doc, 1, migrate_additive, nullptr, nullptr};
        config.register_document(c);
    }
    MockHw hw;
    LightingTask t;
    t.register_channel(make("a", LightType::kDimmer, "pca9685", "pca", 0));
    t.register_channel(make("b", LightType::kDimmer, "pca9685", "pca", 1));
    t.attach_hardware(&hw);
    RoomManager rooms(t.channel_manager(), t, config);
    GroupManager groups(t.channel_manager(), t, config);
    SceneManager scenes(t.channel_manager(), t, config);

    Room r;
    r.id = "room";
    r.name = "Room";
    r.channel_ids = {"a", "b"};
    TEST_ASSERT_EQUAL(static_cast<int>(RoomResult::kOk), static_cast<int>(rooms.register_room(r)));
    rooms.set_power("room", true, 0, "test");
    t.run_once(0);
    TEST_ASSERT_EQUAL_UINT16(4095, hw.pwm.duty[0]);
    TEST_ASSERT_EQUAL_UINT16(4095, hw.pwm.duty[1]);

    Group g;
    g.id = "g";
    g.name = "G";
    g.channel_ids = {"b"};
    TEST_ASSERT_EQUAL(static_cast<int>(GroupResult::kOk), static_cast<int>(groups.register_group(g)));
    groups.set_brightness("g", 25, 0, "test");
    t.run_once(20);
    TEST_ASSERT_EQUAL_UINT16(4095, hw.pwm.duty[0]);
    TEST_ASSERT_EQUAL_UINT16(pca12(25), hw.pwm.duty[1]);

    Scene sc;
    sc.id = "night";
    sc.name = "Night";
    SceneLightState sa;
    sa.light_id = "a";
    sa.power = false;
    SceneLightState sb;
    sb.light_id = "b";
    sb.power = true;
    sb.brightness = 10;
    sc.states = {sa, sb};
    TEST_ASSERT_EQUAL(static_cast<int>(SceneResult::kOk), static_cast<int>(scenes.register_scene(sc)));
    scenes.activate_scene("night");
    t.run_once(40);
    TEST_ASSERT_EQUAL_UINT16(0, hw.pwm.duty[0]);
    TEST_ASSERT_EQUAL_UINT16(pca12(10), hw.pwm.duty[1]);

    // Persistence round trip keeps room membership (regression: light ids
    // used to be dropped on reload).
    RoomManager reloaded(t.channel_manager(), t, config);
    TEST_ASSERT_NOT_NULL(reloaded.find("room"));
    TEST_ASSERT_EQUAL(2, static_cast<int>(reloaded.find("room")->channel_ids.size()));
    SceneManager scenes2(t.channel_manager(), t, config);
    TEST_ASSERT_EQUAL(2, static_cast<int>(scenes2.find("night")->states.size()));
    delete storage;
    delete json;
}


namespace
{
struct ConfigSpy
{
    int config = 0;
    std::string last_id;
};
void spy_listener(const Channel &ch, LightChange change, void *user)
{
    auto *spy = static_cast<ConfigSpy *>(user);
    if (change == LightChange::kConfig) {
        spy->config++;
        spy->last_id = ch.id;
    }
}
} // namespace

static void test_remove_channel_notifies_config_and_releases_output(void)
{
    MockHw hw;
    LightingTask t;
    ConfigSpy spy;
    t.set_listener(spy_listener, &spy);
    t.register_channel(make("a", LightType::kDimmer, "pca9685", "pca", 0));
    t.register_channel(make("b", LightType::kDimmer, "pca9685", "pca", 1));
    t.attach_hardware(&hw);
    LightState s{};
    s.brightness = 80;
    cmd(t, "a", s, kFieldBrightness);
    t.run_once(0);
    TEST_ASSERT_TRUE(hw.pwm.duty[0] > 0);
    spy.config = 0;
    TEST_ASSERT_TRUE(t.remove_channel("a"));
    TEST_ASSERT_EQUAL(1, spy.config);           // MQTT/WS rediscover on delete
    TEST_ASSERT_EQUAL_STRING("a", spy.last_id.c_str());
    TEST_ASSERT_EQUAL_UINT16(0, hw.pwm.duty[0]); // removed light goes dark
    TEST_ASSERT_FALSE(t.remove_channel("a"));
    // The remaining light still maps to its own output after the shift.
    cmd(t, "b", s, kFieldBrightness);
    t.run_once(20);
    TEST_ASSERT_EQUAL_UINT16(pca12(80), hw.pwm.duty[1]);
}

static void test_light_store_validation_and_round_trip(void)
{
    BoardProfile profile;
    std::string json;
    for (const auto &b : builtin_boards()) {
        if (std::string(b.id) == "kincony_kc868_ap") json = b.json;
    }
    TEST_ASSERT_TRUE(load_board_profile(json, profile));
    LightingTask t;
    Channel rgb = make("rgb", LightType::kRgb, "pca9685", "pca9685_0", 4);
    std::string err;
    TEST_ASSERT_TRUE(light_store::validate_against(rgb, profile, t.channel_manager(), "", err));
    t.register_channel(rgb);
    Channel overlap = make("x", LightType::kDimmer, "pca9685", "pca9685_0", 6);
    TEST_ASSERT_FALSE(light_store::validate_against(overlap, profile, t.channel_manager(), "", err));
    Channel overflow = make("y", LightType::kRgbw, "pca9685", "pca9685_0", 13);
    TEST_ASSERT_FALSE(light_store::validate_against(overflow, profile, t.channel_manager(), "", err));
    Channel relay_dimmer = make("z", LightType::kDimmer, "relay", "relay1", 0);
    TEST_ASSERT_FALSE(light_store::validate_against(relay_dimmer, profile, t.channel_manager(), "", err));
    Channel relay_switch = make("s", LightType::kSwitch, "relay", "relay2", 0);
    TEST_ASSERT_TRUE(light_store::validate_against(relay_switch, profile, t.channel_manager(), "", err));

    cJSON *o = light_store::channel_to_json(rgb, false);
    Channel back;
    TEST_ASSERT_TRUE(light_store::channel_from_json(o, back, err));
    cJSON_Delete(o);
    TEST_ASSERT_EQUAL_STRING("pca9685_0", back.driver.device_id.c_str());
    TEST_ASSERT_EQUAL(4, back.driver.channel);
    TEST_ASSERT_EQUAL(static_cast<int>(LightType::kRgb), static_cast<int>(back.type));

    cJSON *bad = cJSON_Parse("{\"brightness\":101}");
    LightCommand c;
    TEST_ASSERT_FALSE(light_store::command_from_json(bad, c, err));
    cJSON_Delete(bad);
    cJSON *good = cJSON_Parse("{\"brightness\":40,\"transitionMs\":300}");
    TEST_ASSERT_TRUE(light_store::command_from_json(good, c, err));
    TEST_ASSERT_EQUAL(kFieldBrightness, c.fields);
    TEST_ASSERT_EQUAL_UINT32(300, c.transition_ms);
    cJSON_Delete(good);
}

namespace
{
class MockFactory final : public IDriverFactory
{
  public:
    std::unique_ptr<II2CBus> create_i2c(const BoardResource &) override
    {
        auto bus = std::make_unique<MockI2cBus>();
        bus->add_device(0x40, {0x00});
        bus->add_device(0x21, {0xFE}); // DI9 active (active-low)
        bus->add_device(0x22, {0xFF});
        last_bus = bus.get();
        return bus;
    }
    std::unique_ptr<IRelayDriver> create_relay(const BoardResource &, bool) override
    {
        return std::make_unique<MockRelay>();
    }
    std::unique_ptr<IPwmDriver> create_pwm(const BoardResource &) override
    {
        return std::make_unique<MockPwm>();
    }
    std::unique_ptr<IDigitalInputDriver> create_input(const BoardResource &) override
    {
        return nullptr; // unsupported in this mock: must not break startup
    }
    MockI2cBus *last_bus = nullptr;
};
} // namespace

static void test_hardware_manager_builds_kc868_profile(void)
{
    BoardProfile profile;
    for (const auto &b : builtin_boards()) {
        if (std::string(b.id) == "kincony_kc868_ap") load_board_profile(b.json, profile);
    }
    MockFactory factory;
    HardwareManager hw(factory);
    TEST_ASSERT_EQUAL(ESP_OK, hw.start(profile));
    TEST_ASSERT_NOT_NULL(hw.driver("pca9685_0"));
    TEST_ASSERT_NOT_NULL(hw.driver("relay1"));
    TEST_ASSERT_NOT_NULL(hw.driver("relay2"));
    TEST_ASSERT_NOT_NULL(hw.i2c_bus("i2c0"));
    // 16 PCF8574 inputs + 3 GPIO inputs (GPIO factory returns null here).
    TEST_ASSERT_EQUAL(19, static_cast<int>(hw.inputs().size()));
    TEST_ASSERT_EQUAL_STRING("DI1", hw.inputs()[0].name.c_str());
    TEST_ASSERT_EQUAL_STRING("DI9", hw.inputs()[8].name.c_str());
    // PCF8574 pins released with 0xFF before reading (regression).
    bool released = false;
    for (const auto &tx : factory.last_bus->transactions()) {
        if ((tx.device_addr == 0x21 || tx.device_addr == 0x22) && tx.write && tx.data.size() == 1 &&
            tx.data[0] == 0xFF) {
            released = true;
        }
    }
    TEST_ASSERT_TRUE(released);
    // DI9 seeded from the first read: 0x21 bit0 low -> active.
    TEST_ASSERT_TRUE(hw.inputs()[8].state);
    TEST_ASSERT_FALSE(hw.inputs()[0].state);
    // 3 GPIO inputs have no driver in this mock -> counted as failures, but
    // every other peripheral still started (bounded startup).
    TEST_ASSERT_EQUAL(3, static_cast<int>(hw.failed_count()));
    TEST_ASSERT_EQUAL(16 + 2, static_cast<int>(hw.outputs().size()));
    TEST_ASSERT_EQUAL(ESP_OK, hw.set_output("pca9685_0:2", 65535));
    hw.stop();
    TEST_ASSERT_FALSE(hw.running());
}

static void test_builtin_profiles_validate_with_bus_pins(void)
{
    std::string json;
    for (const auto &b : builtin_boards()) {
        BoardProfile p;
        TEST_ASSERT_TRUE_MESSAGE(load_board_profile(b.json, p), b.id);
        // I2C buses must declare their pins.
        for (const auto &r : p.resources) {
            if (r.type == "i2c") {
                TEST_ASSERT_TRUE_MESSAGE(r.pin("sda") >= 0 && r.pin("scl") >= 0, b.id);
            }
        }
    }
    // A profile that reuses SCL for a PWM output must be rejected.
    const char *conflict = R"({"schema_version":1,"id":"bad","name":"Bad","mcu":"esp32",
        "resources":[{"id":"i2c0","type":"i2c","pins":{"sda":4,"scl":16}},
                     {"id":"pwm0","type":"pwm","gpio":16,"direction":"output"}],
        "peripherals":[]})";
    IStorage *storage = create_posix_fs(tmpdir("boards"));
    IJson *js = create_cjson_adapter();
    NoopLock lock;
    Clock clock;
    Event pool[4];
    EventBus bus(lock, clock, pool, 4);
    ConfigManager config(*storage, *js, bus);
    BoardService boards(*storage, config);
    boards.init();
    BoardProfile parsed;
    bool ok = false;
    ValidationReport report = boards.validate(conflict, parsed, ok);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_TRUE(report.has_errors());
    ValidationReport create_report;
    std::string id;
    TEST_ASSERT_EQUAL(static_cast<int>(BoardOpResult::kInvalid),
                      static_cast<int>(boards.create(conflict, create_report, id)));
    TEST_ASSERT_EQUAL(static_cast<int>(BoardOpResult::kReadOnly),
                      static_cast<int>(boards.remove("kincony_kc868_ap")));
    delete storage;
    delete js;
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_dimmer_command_reaches_hardware);
    RUN_TEST(test_rgb_uses_consecutive_channels_from_offset);
    RUN_TEST(test_partial_command_keeps_colour);
    RUN_TEST(test_transition_interpolates_then_snaps);
    RUN_TEST(test_newer_command_replaces_transition);
    RUN_TEST(test_redundant_writes_skipped);
    RUN_TEST(test_relay_switch_and_unknown_driver);
    RUN_TEST(test_effect_runs_and_plain_command_cancels);
    RUN_TEST(test_cct_mixes_warm_and_cool);
    RUN_TEST(test_room_group_scene_fan_out_reaches_hardware);
    RUN_TEST(test_remove_channel_notifies_config_and_releases_output);
    RUN_TEST(test_light_store_validation_and_round_trip);
    RUN_TEST(test_hardware_manager_builds_kc868_profile);
    RUN_TEST(test_builtin_profiles_validate_with_bus_pins);
    return UNITY_END();
}
