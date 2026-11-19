// Host tests for Phase 5 — Rooms, Groups, Scenes + Color Engine + Transition Engine
#include <cstring>
#include <string>
#include <vector>

#include <unity.h>

#include "core/cJSON.h"

#include "lumen/lighting/channel.h"
#include "lumen/lighting/color_engine.h"
#include "lumen/lighting/group_manager.h"
#include "lumen/lighting/lighting_task.h"
#include "lumen/lighting/room_manager.h"
#include "lumen/lighting/scene_manager.h"
#include "lumen/core/config_manager.hpp"
#include "lumen/core/storage.hpp"

using lumen::Channel;
using lumen::ChannelManager;
using lumen::ConfigManager;
using lumen::DocConfig;
using lumen::Group;
using lumen::GroupManager;
using lumen::GroupResult;
using lumen::LightCommand;
using lumen::LightCommandEnvelope;
using lumen::LightState;
using lumen::LightingTask;
using lumen::Room;
using lumen::RoomManager;
using lumen::RoomResult;
using lumen::Rgb8;
using lumen::Rgbw8;
using lumen::HsvState;
using lumen::ChannelCalibration;
using lumen::Transition;
using lumen::ApplyResult;
using lumen::ease_linear;
using lumen::ease_smooth;
using lumen::ease_inout_cubic;
using lumen::transition_tick;
using lumen::apply_command;
using lumen::apply_gamma;
using lumen::Scene;
using lumen::SceneLightState;
using lumen::SceneManager;
using lumen::SceneResult;
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
using lumen::create_posix_fs;
using lumen::create_cjson_adapter;
using lumen::IStorage;
using lumen::IJson;
using lumen::EventBus;
using lumen::ILock;
using lumen::IClock;
using lumen::Event;

// Test ports
struct NoopLock final : lumen::ILock {
    void lock() override {}
    void unlock() override {}
};
struct FakeClock final : lumen::IClock {
    uint32_t now = 0;
    uint32_t ms() override { return now; }
};
static NoopLock g_lock;
static FakeClock g_clock;
static Event g_pool[64];

// Migration helper for test configs — add schema_version to v0 docs.
static bool test_migrate_add_schema_version(std::string &doc, uint16_t from, uint16_t to, IJson &json)
{
    (void)from;
    lumen::JsonDoc *d = json.parse(doc);
    if (d == nullptr) {
        return false;
    }
    json.set_int(d, "schema_version", static_cast<int32_t>(to));
    doc = json.stringify(d);
    json.free(d);
    return !doc.empty();
}

// Helper to create a ConfigManager backed by a temp directory with standard docs registered.
// Uses a dedicated EventBus per test to avoid cross-test event queue saturation.
// Returns a pointer to a heap-allocated ConfigManager to avoid reference lifetime issues.
static ConfigManager* make_test_config(const std::string& test_name) {
    static int counter = 0;
    std::string tmp_path = "/tmp/lumen_test_" + test_name + "_" +
                           std::to_string(++counter);
    // Heap-allocate storage and json so they outlive the ConfigManager.
    IStorage* storage = create_posix_fs(tmp_path);
    IJson* json = create_cjson_adapter();
    // Per-test event pool (larger capacity to avoid overflow from ConfigChanged posts).
    static Event test_pool[256];
    static EventBus test_bus(g_lock, g_clock, test_pool, 256);
    ConfigManager* config = new ConfigManager(*storage, *json, test_bus);

    // Register standard config documents so managers can save/load.
    DocConfig rooms_doc{};
    rooms_doc.path = "rooms.json";
    rooms_doc.latest_schema_version = 1;
    rooms_doc.default_doc = []() { return std::string("{\"rooms\":[]}"); };
    rooms_doc.migrate = test_migrate_add_schema_version;
    config->register_document(rooms_doc);

    DocConfig groups_doc{};
    groups_doc.path = "groups.json";
    groups_doc.latest_schema_version = 1;
    groups_doc.default_doc = []() { return std::string("{\"groups\":[]}"); };
    groups_doc.migrate = test_migrate_add_schema_version;
    config->register_document(groups_doc);

    DocConfig scenes_doc{};
    scenes_doc.path = "scenes.json";
    scenes_doc.latest_schema_version = 1;
    scenes_doc.default_doc = []() { return std::string("{\"scenes\":[]}"); };
    scenes_doc.migrate = test_migrate_add_schema_version;
    config->register_document(scenes_doc);

    return config;
}

// ========================================================================
// Room manager tests
// ---------------------------------------------------------------------------
static void test_room_register_and_find(void)
{
    ChannelManager cm;
    LightingTask lt;
    // Use dedicated event pool like make_test_config
    static Event test_pool[256];
    static EventBus bus(g_lock, g_clock, test_pool, 256);
    // Clean up any leftover test directory
    std::string test_dir = "/tmp/lumen_test_room_reg_find";
    std::string cmd = "rm -rf \"" + test_dir + "\"";
    std::system(cmd.c_str());
    
    ConfigManager config(*create_posix_fs(test_dir),
                         *create_cjson_adapter(), bus);

    DocConfig rooms_doc{};
    rooms_doc.path = "rooms.json";
    rooms_doc.latest_schema_version = 1;
    rooms_doc.default_doc = []() { return std::string("{\"rooms\":[]}"); };
    rooms_doc.migrate = test_migrate_add_schema_version;
    config.register_document(rooms_doc);

    RoomManager rm(cm, lt, config);

    Room r{};
    r.id = "r1";
    r.name = "Kitchen";
    RoomResult rr = rm.register_room(r);
    TEST_ASSERT_EQUAL(RoomResult::kOk, rr);

    const Room *found = rm.find("r1");
    TEST_ASSERT_NOT_NULL(found);
    TEST_ASSERT_EQUAL_STRING("Kitchen", found->name.c_str());

    TEST_ASSERT_NULL(rm.find("nonexistent"));
}

static void test_room_duplicate_rejected(void)
{
    ChannelManager cm;
    LightingTask lt;
    // Use dedicated event pool like make_test_config
    static Event test_pool[256];
    static EventBus bus(g_lock, g_clock, test_pool, 256);
    // Clean up any leftover test directory
    std::string test_dir = "/tmp/lumen_test_room_dup";
    std::string cmd = "rm -rf \"" + test_dir + "\"";
    std::system(cmd.c_str());
    
    ConfigManager config(*create_posix_fs(test_dir),
                         *create_cjson_adapter(), bus);

    DocConfig rooms_doc{};
    rooms_doc.path = "rooms.json";
    rooms_doc.latest_schema_version = 1;
    rooms_doc.default_doc = []() { return std::string("{\"rooms\":[]}"); };
    rooms_doc.migrate = test_migrate_add_schema_version;
    config.register_document(rooms_doc);

    DocConfig groups_doc{};
    groups_doc.path = "groups.json";
    groups_doc.latest_schema_version = 1;
    groups_doc.default_doc = []() { return std::string("{\"groups\":[]}"); };
    groups_doc.migrate = test_migrate_add_schema_version;
    config.register_document(groups_doc);

    DocConfig scenes_doc{};
    scenes_doc.path = "scenes.json";
    scenes_doc.latest_schema_version = 1;
    scenes_doc.default_doc = []() { return std::string("{\"scenes\":[]}"); };
    scenes_doc.migrate = test_migrate_add_schema_version;
    config.register_document(scenes_doc);
    RoomManager rm(cm, lt, config);

    Room r{};
    r.id = "r1";
    r.name = "Kitchen";
    TEST_ASSERT_EQUAL(RoomResult::kOk, rm.register_room(r));
    TEST_ASSERT_EQUAL(RoomResult::kDuplicate, rm.register_room(r));
}

static void test_room_add_remove_light(void)
{
    ChannelManager cm;
    LightingTask lt;
    ConfigManager* config = make_test_config("room3");
    RoomManager rm(cm, lt, *config);

    Channel c{};
    c.id = "light1";
    c.name = "Light 1";
    c.type = lumen::LightType::kDimmer;
    cm.register_channel(c);

    Room r{};
    r.id = "r1";
    r.name = "Kitchen";
    rm.register_room(r);

    TEST_ASSERT_EQUAL(RoomResult::kOk, rm.add_light("r1", "light1"));
    TEST_ASSERT_EQUAL(RoomResult::kOk, rm.add_light("r1", "light1"));
    TEST_ASSERT_EQUAL(RoomResult::kInvalid, rm.add_light("r1", "nonexistent"));
    TEST_ASSERT_EQUAL(RoomResult::kNotFound, rm.add_light("nope", "light1"));

    TEST_ASSERT_EQUAL(RoomResult::kOk, rm.remove_light("r1", "light1"));
    TEST_ASSERT_EQUAL(RoomResult::kOk, rm.remove_light("r1", "light1"));
}

static void test_room_remove_detaches_lights(void)
{
    ChannelManager cm;
    LightingTask lt;
    ConfigManager* config = make_test_config("room4");
    RoomManager rm(cm, lt, *config);

    Channel c{};
    c.id = "light1";
    c.name = "Light 1";
    c.type = lumen::LightType::kDimmer;
    cm.register_channel(c);

    Room r{};
    r.id = "r1";
    r.channel_ids.push_back("light1");
    rm.register_room(r);

    TEST_ASSERT_EQUAL(RoomResult::kOk, rm.remove_room("r1"));
    TEST_ASSERT_NULL(rm.find("r1"));

    TEST_ASSERT_NOT_NULL(cm.find("light1"));
}

static void test_room_master_power(void)
{
    ChannelManager cm;
    LightingTask lt;
    ConfigManager* config = make_test_config("room5");
    RoomManager rm(cm, lt, *config);

    Channel c1{};
    c1.id = "light1";
    c1.name = "Light 1";
    c1.type = lumen::LightType::kDimmer;
    Channel c2{};
    c2.id = "light2";
    c2.name = "Light 2";
    c2.type = lumen::LightType::kDimmer;
    cm.register_channel(c1);
    cm.register_channel(c2);

    Room r{};
    r.id = "r1";
    r.channel_ids.push_back("light1");
    r.channel_ids.push_back("light2");
    rm.register_room(r);

    TEST_ASSERT_EQUAL(RoomResult::kOk, rm.set_power("r1", true, 0, "test"));
    TEST_ASSERT_EQUAL(RoomResult::kOk, rm.set_power("r1", false, 0, "test"));
    TEST_ASSERT_EQUAL(RoomResult::kNotFound, rm.set_power("nope", true, 0, "test"));
}

static void test_room_master_brightness(void)
{
    ChannelManager cm;
    LightingTask lt;
    ConfigManager* config = make_test_config("room6");
    RoomManager rm(cm, lt, *config);

    Channel c1{};
    c1.id = "light1";
    c1.name = "Light 1";
    c1.type = lumen::LightType::kDimmer;
    cm.register_channel(c1);

    Room r{};
    r.id = "r1";
    r.channel_ids.push_back("light1");
    rm.register_room(r);

    TEST_ASSERT_EQUAL(RoomResult::kOk, rm.set_brightness("r1", 75, 0, "test"));
    TEST_ASSERT_EQUAL(RoomResult::kNotFound, rm.set_brightness("nope", 50, 0, "test"));
}

// ========================================================================
// Group manager tests
// ---------------------------------------------------------------------------
static void test_group_register_and_find(void)
{
    ChannelManager cm;
    LightingTask lt;
    // Use dedicated event pool like make_test_config
    static Event test_pool[256];
    static EventBus bus(g_lock, g_clock, test_pool, 256);
    // Clean up any leftover test directory
    std::string test_dir = "/tmp/lumen_test_gm_reg";
    std::string cmd = "rm -rf \"" + test_dir + "\"";
    std::system(cmd.c_str());
    
    ConfigManager config(*create_posix_fs(test_dir),
                         *create_cjson_adapter(), bus);

    DocConfig rooms_doc{};
    rooms_doc.path = "rooms.json";
    rooms_doc.latest_schema_version = 1;
    rooms_doc.default_doc = []() { return std::string("{\"rooms\":[]}"); };
    rooms_doc.migrate = test_migrate_add_schema_version;
    config.register_document(rooms_doc);

    DocConfig groups_doc{};
    groups_doc.path = "groups.json";
    groups_doc.latest_schema_version = 1;
    groups_doc.default_doc = []() { return std::string("{\"groups\":[]}"); };
    groups_doc.migrate = test_migrate_add_schema_version;
    config.register_document(groups_doc);

    DocConfig scenes_doc{};
    scenes_doc.path = "scenes.json";
    scenes_doc.latest_schema_version = 1;
    scenes_doc.default_doc = []() { return std::string("{\"scenes\":[]}"); };
    scenes_doc.migrate = test_migrate_add_schema_version;
    config.register_document(scenes_doc);
    GroupManager gm(cm, lt, config);

    Group g{};
    g.id = "g1";
    g.name = "Downstairs";
    GroupResult gr = gm.register_group(g);
    TEST_ASSERT_EQUAL(GroupResult::kOk, gr);

    const Group *found = gm.find("g1");
    TEST_ASSERT_NOT_NULL(found);
    TEST_ASSERT_EQUAL_STRING("Downstairs", found->name.c_str());

    TEST_ASSERT_NULL(gm.find("nonexistent"));
}

static void test_group_add_remove_light(void)
{
    ChannelManager cm;
    LightingTask lt;
    ConfigManager* config = make_test_config("gm2");
    GroupManager gm(cm, lt, *config);

    Channel c{};
    c.id = "light1";
    c.name = "Light 1";
    c.type = lumen::LightType::kDimmer;
    cm.register_channel(c);

    Group g{};
    g.id = "g1";
    g.name = "Downstairs";
    gm.register_group(g);

    TEST_ASSERT_EQUAL(GroupResult::kOk, gm.add_light("g1", "light1"));
    TEST_ASSERT_EQUAL(GroupResult::kOk, gm.add_light("g1", "light1"));
    TEST_ASSERT_EQUAL(GroupResult::kInvalid, gm.add_light("g1", "nonexistent"));
    TEST_ASSERT_EQUAL(GroupResult::kNotFound, gm.add_light("nope", "light1"));

    TEST_ASSERT_EQUAL(GroupResult::kOk, gm.remove_light("g1", "light1"));
}

static void test_group_remove_detaches_lights(void)
{
    ChannelManager cm;
    LightingTask lt;
    ConfigManager* config = make_test_config("gm3");
    GroupManager gm(cm, lt, *config);

    Channel c{};
    c.id = "light1";
    c.name = "Light 1";
    c.type = lumen::LightType::kDimmer;
    cm.register_channel(c);

    Group g{};
    g.id = "g1";
    g.channel_ids.push_back("light1");
    gm.register_group(g);

    TEST_ASSERT_EQUAL(GroupResult::kOk, gm.remove_group("g1"));
    TEST_ASSERT_NULL(gm.find("g1"));

    TEST_ASSERT_NOT_NULL(cm.find("light1"));
}

static void test_group_set_power(void)
{
    ChannelManager cm;
    LightingTask lt;
    ConfigManager* config = make_test_config("gm4");
    GroupManager gm(cm, lt, *config);

    Channel c1{};
    c1.id = "light1";
    c1.name = "Light 1";
    c1.type = lumen::LightType::kDimmer;
    Channel c2{};
    c2.id = "light2";
    c2.name = "Light 2";
    c2.type = lumen::LightType::kDimmer;
    cm.register_channel(c1);
    cm.register_channel(c2);

    Group g{};
    g.id = "g1";
    g.channel_ids.push_back("light1");
    g.channel_ids.push_back("light2");
    gm.register_group(g);

    TEST_ASSERT_EQUAL(GroupResult::kOk, gm.set_power("g1", true, 0, "test", "cmd1"));
    TEST_ASSERT_EQUAL(GroupResult::kOk, gm.set_power("g1", false, 0, "test", "cmd2"));
    TEST_ASSERT_EQUAL(GroupResult::kNotFound, gm.set_power("nope", true, 0, "test", "cmd3"));
}

// ---------------------------------------------------------------------------
// Group aggregation tests
// ---------------------------------------------------------------------------
static void test_group_aggregate_dimmer_only(void)
{
    ChannelManager cm;
    LightingTask lt;
    ConfigManager* config = make_test_config("gm5");
    GroupManager gm(cm, lt, *config);

    Channel c1{};
    c1.id = "light1";
    c1.name = "Light 1";
    c1.type = lumen::LightType::kDimmer;
    Channel c2{};
    c2.id = "light2";
    c2.name = "Light 2";
    c2.type = lumen::LightType::kDimmer;
    cm.register_channel(c1);
    cm.register_channel(c2);

    LightState s1{};
    s1.power = true;
    s1.brightness = 80;
    cm.apply_patch("light1", s1, s1);

    LightState s2{};
    s2.power = true;
    s2.brightness = 40;
    cm.apply_patch("light2", s2, s2);

    Group g{};
    g.id = "g1";
    g.channel_ids.push_back("light1");
    g.channel_ids.push_back("light2");
    gm.register_group(g);

    uint8_t on_count = 0;
    uint8_t mean_brightness = 0;
    TEST_ASSERT_TRUE(gm.aggregate_state("g1", on_count, mean_brightness));
    TEST_ASSERT_EQUAL(2, on_count);
    TEST_ASSERT_EQUAL(60, mean_brightness);
}

static void test_group_aggregate_mixed_types(void)
{
    ChannelManager cm;
    LightingTask lt;
    ConfigManager* config = make_test_config("gm6");
    GroupManager gm(cm, lt, *config);

    Channel c1{};
    c1.id = "light1";
    c1.name = "Dimmer 1";
    c1.type = lumen::LightType::kDimmer;
    Channel c2{};
    c2.id = "light2";
    c2.name = "RGB 1";
    c2.type = lumen::LightType::kRgb;
    cm.register_channel(c1);
    cm.register_channel(c2);

    LightState s1{};
    s1.power = true;
    s1.brightness = 100;
    cm.apply_patch("light1", s1, s1);

    LightState s2{};
    s2.power = true;
    s2.brightness = 50;
    cm.apply_patch("light2", s2, s2);

    Group g{};
    g.id = "g1";
    g.channel_ids.push_back("light1");
    g.channel_ids.push_back("light2");
    gm.register_group(g);

    uint8_t on_count = 0;
    uint8_t mean_brightness = 0;
    TEST_ASSERT_TRUE(gm.aggregate_state("g1", on_count, mean_brightness));
    TEST_ASSERT_EQUAL(2, on_count);
    TEST_ASSERT_EQUAL(75, mean_brightness);
}

// ========================================================================
// Scene manager tests
// ---------------------------------------------------------------------------
static void test_scene_register_and_find(void)
{
    ChannelManager cm;
    LightingTask lt;
    // Use dedicated event pool like make_test_config
    static Event test_pool[256];
    static EventBus bus(g_lock, g_clock, test_pool, 256);
    // Clean up any leftover test directory
    std::string test_dir = "/tmp/lumen_test_sm_reg";
    std::string cmd = "rm -rf \"" + test_dir + "\"";
    std::system(cmd.c_str());
    
    ConfigManager config(*create_posix_fs(test_dir),
                         *create_cjson_adapter(), bus);

    DocConfig rooms_doc{};
    rooms_doc.path = "rooms.json";
    rooms_doc.latest_schema_version = 1;
    rooms_doc.default_doc = []() { return std::string("{\"rooms\":[]}"); };
    rooms_doc.migrate = test_migrate_add_schema_version;
    config.register_document(rooms_doc);

    DocConfig groups_doc{};
    groups_doc.path = "groups.json";
    groups_doc.latest_schema_version = 1;
    groups_doc.default_doc = []() { return std::string("{\"groups\":[]}"); };
    groups_doc.migrate = test_migrate_add_schema_version;
    config.register_document(groups_doc);

    DocConfig scenes_doc{};
    scenes_doc.path = "scenes.json";
    scenes_doc.latest_schema_version = 1;
    scenes_doc.default_doc = []() { return std::string("{\"scenes\":[]}"); };
    scenes_doc.migrate = test_migrate_add_schema_version;
    config.register_document(scenes_doc);
    SceneManager sm(cm, lt, config);

    Scene s{};
    s.id = "scene1";
    s.name = "Movie Night";
    SceneResult sr = sm.register_scene(s);
    TEST_ASSERT_EQUAL(SceneResult::kOk, sr);

    const Scene *found = sm.find("scene1");
    TEST_ASSERT_NOT_NULL(found);
    TEST_ASSERT_EQUAL_STRING("Movie Night", found->name.c_str());

    TEST_ASSERT_NULL(sm.find("nonexistent"));
}

static void test_scene_duplicate_rejected(void)
{
    ChannelManager cm;
    LightingTask lt;
    // Use dedicated event pool like make_test_config
    static Event test_pool[256];
    static EventBus bus(g_lock, g_clock, test_pool, 256);
    // Clean up any leftover test directory
    std::string test_dir = "/tmp/lumen_test_sm_dup";
    std::string cmd = "rm -rf \"" + test_dir + "\"";
    std::system(cmd.c_str());
    
    ConfigManager config(*create_posix_fs(test_dir),
                         *create_cjson_adapter(), bus);

    DocConfig rooms_doc{};
    rooms_doc.path = "rooms.json";
    rooms_doc.latest_schema_version = 1;
    rooms_doc.default_doc = []() { return std::string("{\"rooms\":[]}"); };
    rooms_doc.migrate = test_migrate_add_schema_version;
    config.register_document(rooms_doc);

    DocConfig groups_doc{};
    groups_doc.path = "groups.json";
    groups_doc.latest_schema_version = 1;
    groups_doc.default_doc = []() { return std::string("{\"groups\":[]}"); };
    groups_doc.migrate = test_migrate_add_schema_version;
    config.register_document(groups_doc);

    DocConfig scenes_doc{};
    scenes_doc.path = "scenes.json";
    scenes_doc.latest_schema_version = 1;
    scenes_doc.default_doc = []() { return std::string("{\"scenes\":[]}"); };
    scenes_doc.migrate = test_migrate_add_schema_version;
    config.register_document(scenes_doc);
    SceneManager sm(cm, lt, config);

    Scene s{};
    s.id = "scene1";
    s.name = "Movie Night";
    TEST_ASSERT_EQUAL(SceneResult::kOk, sm.register_scene(s));
    TEST_ASSERT_EQUAL(SceneResult::kDuplicate, sm.register_scene(s));
}

static void test_scene_update_and_delete(void)
{
    ChannelManager cm;
    LightingTask lt;
    ConfigManager* config = make_test_config("sm3");
    SceneManager sm(cm, lt, *config);

    Channel c{};
    c.id = "light1";
    c.name = "Light 1";
    c.type = lumen::LightType::kDimmer;
    cm.register_channel(c);

    Scene s{};
    s.id = "scene1";
    s.name = "Movie Night";
    sm.register_scene(s);

    std::vector<SceneLightState> states{
        {"light1", true, 50, 0, 0, 0, 0, 0},
    };
    TEST_ASSERT_EQUAL(SceneResult::kOk, sm.update_scene("scene1", states, 1000));

    const Scene *sc = sm.find("scene1");
    TEST_ASSERT_NOT_NULL(sc);
    TEST_ASSERT_EQUAL(1, sc->states.size());
    TEST_ASSERT_EQUAL_STRING("light1", sc->states[0].light_id.c_str());
    TEST_ASSERT_EQUAL(50, sc->states[0].brightness);
    TEST_ASSERT_EQUAL(0, sc->states[0].red);
    TEST_ASSERT_EQUAL(1000, sc->transition_ms);

    TEST_ASSERT_EQUAL(SceneResult::kOk, sm.delete_scene("scene1"));
    TEST_ASSERT_NULL(sm.find("scene1"));
}

static void test_scene_update_rejects_unknown_light(void)
{
    ChannelManager cm;
    LightingTask lt;
    ConfigManager* config = make_test_config("sm4");
    SceneManager sm(cm, lt, *config);

    Scene s{};
    s.id = "scene1";
    s.name = "Movie Night";
    sm.register_scene(s);

    std::vector<SceneLightState> states{
        {"nonexistent", true, 50, 0, 0, 0, 0, 0},
    };
    TEST_ASSERT_EQUAL(SceneResult::kInvalid, sm.update_scene("scene1", states, 0));
}

static void test_scene_update_rejects_unsupported_property(void)
{
    ChannelManager cm;
    LightingTask lt;
    ConfigManager* config = make_test_config("sm5");
    SceneManager sm(cm, lt, *config);

    Channel c{};
    c.id = "light1";
    c.name = "Dimmer 1";
    c.type = lumen::LightType::kDimmer;
    cm.register_channel(c);

    Scene s{};
    s.id = "scene1";
    s.name = "Movie Night";
    sm.register_scene(s);

    std::vector<SceneLightState> states{
        {"light1", true, 50, 200, 100, 50, 0, 0},
    };
    TEST_ASSERT_EQUAL(SceneResult::kInvalid, sm.update_scene("scene1", states, 0));
}

static void test_scene_activate(void)
{
    ChannelManager cm;
    LightingTask lt;
    ConfigManager* config = make_test_config("sm6");
    SceneManager sm(cm, lt, *config);

    Channel c{};
    c.id = "light1";
    c.name = "Dimmer 1";
    c.type = lumen::LightType::kDimmer;
    cm.register_channel(c);

    Scene s{};
    s.id = "scene1";
    s.name = "Movie Night";
    s.states.push_back({"light1", true, 75, 0, 0, 0, 0, 0});
    sm.register_scene(s);

    TEST_ASSERT_EQUAL(SceneResult::kOk, sm.activate_scene("scene1", 0, "test", "cmd1"));
    TEST_ASSERT_EQUAL(SceneResult::kNotFound, sm.activate_scene("nope", 0, "test", "cmd2"));
}

// Persistence round-trip tests
// ---------------------------------------------------------------------------
static void test_room_persist_round_trip(void)
{
    ChannelManager cm;
    LightingTask lt;
    static EventBus bus(g_lock, g_clock, g_pool, 64);
    ConfigManager config(*create_posix_fs("/tmp/lumen_test_config_1"),
                         *create_cjson_adapter(), bus);

    DocConfig rooms_doc{};
    rooms_doc.path = "rooms.json";
    rooms_doc.latest_schema_version = 1;
    rooms_doc.default_doc = []() { return std::string("{\"rooms\":[]}"); };
    rooms_doc.migrate = test_migrate_add_schema_version;
    config.register_document(rooms_doc);

    Channel c{};
    c.id = "light1";
    c.name = "Light 1";
    c.type = lumen::LightType::kDimmer;
    cm.register_channel(c);

    RoomManager rm(cm, lt, config);

    Room r{};
    r.id = "kitchen";
    r.name = "Kitchen";
    rm.register_room(r);
    rm.add_light("kitchen", "light1");

    Room r2{};
    r2.id = "bedroom";
    r2.name = "Bedroom";
    rm.register_room(r2);

    // Reboot: new config manager reading same storage
    static EventBus bus2(g_lock, g_clock, g_pool, 64);
    ConfigManager config2(*create_posix_fs("/tmp/lumen_test_config_1"),
                          *create_cjson_adapter(), bus2);
    DocConfig rooms_doc2{};
    rooms_doc2.path = "rooms.json";
    rooms_doc2.latest_schema_version = 1;
    rooms_doc2.default_doc = []() { return std::string("{\"rooms\":[]}"); };
    rooms_doc2.migrate = test_migrate_add_schema_version;
    config2.register_document(rooms_doc2);

    ChannelManager cm2;
    LightingTask lt2;
    RoomManager rm2(cm2, lt2, config2);

    TEST_ASSERT_NOT_NULL(rm2.find("kitchen"));
    const Room *rk = rm2.find("kitchen");
    TEST_ASSERT_EQUAL_STRING("Kitchen", rk->name.c_str());
    TEST_ASSERT_EQUAL(1, rk->channel_ids.size());
    TEST_ASSERT_EQUAL_STRING("light1", rk->channel_ids[0].c_str());

    TEST_ASSERT_NOT_NULL(rm2.find("bedroom"));
}

static void test_group_persist_round_trip(void)
{
    ChannelManager cm;
    LightingTask lt;
    static EventBus bus(g_lock, g_clock, g_pool, 64);
    ConfigManager config(*create_posix_fs("/tmp/lumen_test_config_2"),
                         *create_cjson_adapter(), bus);

    DocConfig groups_doc{};
    groups_doc.path = "groups.json";
    groups_doc.latest_schema_version = 1;
    groups_doc.default_doc = []() { return std::string("{\"groups\":[]}"); };
    groups_doc.migrate = test_migrate_add_schema_version;
    config.register_document(groups_doc);

    GroupManager gm(cm, lt, config);

    // Register channel in ChannelManager first
    Channel c{};
    c.id = "light1";
    c.name = "Light 1";
    c.type = lumen::LightType::kDimmer;
    cm.register_channel(c);

    Group g{};
    g.id = "g1";
    g.name = "Downstairs";
    gm.register_group(g);
    gm.add_light("g1", "light1");

    // Reboot: new config manager reading same storage
    static EventBus bus2(g_lock, g_clock, g_pool, 64);
    ConfigManager config2(*create_posix_fs("/tmp/lumen_test_config_2"),
                          *create_cjson_adapter(), bus2);
    DocConfig groups_doc2{};
    groups_doc2.path = "groups.json";
    groups_doc2.latest_schema_version = 1;
    groups_doc2.default_doc = []() { return std::string("{\"groups\":[]}"); };
    groups_doc2.migrate = test_migrate_add_schema_version;
    config2.register_document(groups_doc2);

    ChannelManager cm2;
    LightingTask lt2;
    GroupManager gm2(cm2, lt2, config2);

    TEST_ASSERT_NOT_NULL(gm2.find("g1"));
    const Group *g2 = gm2.find("g1");
    TEST_ASSERT_EQUAL_STRING("Downstairs", g2->name.c_str());
    TEST_ASSERT_EQUAL(1, g2->channel_ids.size());
    TEST_ASSERT_EQUAL_STRING("light1", g2->channel_ids[0].c_str());
}

static void test_scene_persist_round_trip(void)
{
    ChannelManager cm;
    LightingTask lt;
    static EventBus bus(g_lock, g_clock, g_pool, 64);
    ConfigManager config(*create_posix_fs("/tmp/lumen_test_config_3"),
                         *create_cjson_adapter(), bus);

    DocConfig scenes_doc{};
    scenes_doc.path = "scenes.json";
    scenes_doc.latest_schema_version = 1;
    scenes_doc.default_doc = []() { return std::string("{\"scenes\":[]}"); };
    scenes_doc.migrate = test_migrate_add_schema_version;
    config.register_document(scenes_doc);

    SceneManager sm(cm, lt, config);

    // Register channel in ChannelManager first - use RGB type to support color
    Channel c{};
    c.id = "light1";
    c.name = "Light 1";
    c.type = lumen::LightType::kRgb;
    cm.register_channel(c);

    Scene s{};
    s.id = "movie_night";
    s.name = "Movie Night";
    s.transition_ms = 1500;
    // RGB type supports RGB but not CCT, so color_temperature must be 0
    s.states.push_back({"light1", true, 30, 200, 100, 50, 0, 0});
    sm.register_scene(s);

    // Reboot: new config manager reading same storage
    static EventBus bus2(g_lock, g_clock, g_pool, 64);
    ConfigManager config2(*create_posix_fs("/tmp/lumen_test_config_3"),
                          *create_cjson_adapter(), bus2);
    DocConfig scenes_doc2{};
    scenes_doc2.path = "scenes.json";
    scenes_doc2.latest_schema_version = 1;
    scenes_doc2.default_doc = []() { return std::string("{\"scenes\":[]}"); };
    scenes_doc2.migrate = test_migrate_add_schema_version;
    config2.register_document(scenes_doc2);

    ChannelManager cm2;
    LightingTask lt2;
    SceneManager sm2(cm2, lt2, config2);

    TEST_ASSERT_NOT_NULL(sm2.find("movie_night"));
    const Scene *s2 = sm2.find("movie_night");
    TEST_ASSERT_EQUAL_STRING("Movie Night", s2->name.c_str());
    TEST_ASSERT_EQUAL(1500, s2->transition_ms);
    TEST_ASSERT_EQUAL(1, s2->states.size());
    TEST_ASSERT_EQUAL_STRING("light1", s2->states[0].light_id.c_str());
    TEST_ASSERT_EQUAL(true, s2->states[0].power);
    TEST_ASSERT_EQUAL(30, s2->states[0].brightness);
    TEST_ASSERT_EQUAL(200, s2->states[0].red);
    TEST_ASSERT_EQUAL(100, s2->states[0].green);
    TEST_ASSERT_EQUAL(50, s2->states[0].blue);
    TEST_ASSERT_EQUAL(0, s2->states[0].color_temperature);
}

// ========================================================================
// ---------------------------------------------------------------------------
// Forward declarations for color engine and transition tests
static void test_gamma_table_bounds(void);
static void test_apply_gamma(void);
static void test_rgb_to_hsv_grayscale(void);
static void test_rgb_to_hsv_red(void);
static void test_rgb_to_hsv_green(void);
static void test_rgb_to_hsv_blue(void);
static void test_hsv_to_rgb_roundtrip(void);
static void test_rgb_to_rgbw(void);
static void test_rgbw_to_rgb(void);
static void test_kelvin_to_rgb_warm(void);
static void test_kelvin_to_rgb_cool(void);
static void test_kelvin_to_rgb_clamp(void);
static void test_rgb_to_kelvin_saturated_returns_zero(void);
static void test_apply_brightness(void);
static void test_apply_brightness_to_state(void);
static void test_apply_calibration(void);
static void test_validate_switch_rejects_color(void);
static void test_validate_dimmer_rejects_color(void);
static void test_validate_rgb_accepts_color(void);
static void test_validate_rgbw_accepts_white(void);
static void test_validate_rgbw_rejects_cct(void);
static void test_validate_cct_accepts_cct(void);
static void test_validate_cct_rejects_rgb(void);
static void test_validate_switch_accepts_brightness(void);
static void test_normalize_clamps_brightness(void);
static void test_normalize_zeros_unsupported_rgb_on_dimmer(void);
static void test_normalize_cct_zeros_rgb(void);
static void test_ease_linear(void);
static void test_ease_smooth(void);
static void test_ease_inout_cubic(void);
static void test_transition_inactive_returns_false(void);
static void test_transition_instant_complete(void);
static void test_transition_linear_interpolation(void);
static void test_transition_smooth_easing(void);
static void test_transition_power_interpolation(void);
static void test_transition_cancel_on_new_command(void);
static void test_apply_command_instant(void);
static void test_apply_command_transition(void);
int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    UNITY_BEGIN();
    // Room tests
    RUN_TEST(test_room_register_and_find);
    RUN_TEST(test_room_duplicate_rejected);
    RUN_TEST(test_room_add_remove_light);
    RUN_TEST(test_room_remove_detaches_lights);
    RUN_TEST(test_room_master_power);
    RUN_TEST(test_room_master_brightness);
    // Group tests
    RUN_TEST(test_group_register_and_find);
    RUN_TEST(test_group_add_remove_light);
    RUN_TEST(test_group_remove_detaches_lights);
    RUN_TEST(test_group_set_power);
    RUN_TEST(test_group_aggregate_dimmer_only);
    RUN_TEST(test_group_aggregate_mixed_types);
    // Scene tests
    RUN_TEST(test_scene_register_and_find);
    RUN_TEST(test_scene_duplicate_rejected);
    RUN_TEST(test_scene_update_and_delete);
    RUN_TEST(test_scene_update_rejects_unknown_light);
    RUN_TEST(test_scene_update_rejects_unsupported_property);
    RUN_TEST(test_scene_activate);
    // Persistence round-trip tests
    RUN_TEST(test_room_persist_round_trip);
    RUN_TEST(test_group_persist_round_trip);
    RUN_TEST(test_scene_persist_round_trip);
    // Color engine tests
    RUN_TEST(test_gamma_table_bounds);
    RUN_TEST(test_apply_gamma);
    RUN_TEST(test_rgb_to_hsv_grayscale);
    RUN_TEST(test_rgb_to_hsv_red);
    RUN_TEST(test_rgb_to_hsv_green);
    RUN_TEST(test_rgb_to_hsv_blue);
    RUN_TEST(test_hsv_to_rgb_roundtrip);
    RUN_TEST(test_rgb_to_rgbw);
    RUN_TEST(test_rgbw_to_rgb);
    RUN_TEST(test_kelvin_to_rgb_warm);
    RUN_TEST(test_kelvin_to_rgb_cool);
    RUN_TEST(test_kelvin_to_rgb_clamp);
    RUN_TEST(test_rgb_to_kelvin_saturated_returns_zero);
    RUN_TEST(test_apply_brightness);
    RUN_TEST(test_apply_brightness_to_state);
    RUN_TEST(test_apply_calibration);
    RUN_TEST(test_validate_switch_rejects_color);
    RUN_TEST(test_validate_dimmer_rejects_color);
    RUN_TEST(test_validate_rgb_accepts_color);
    RUN_TEST(test_validate_rgbw_accepts_white);
    RUN_TEST(test_validate_rgbw_rejects_cct);
    RUN_TEST(test_validate_cct_accepts_cct);
    RUN_TEST(test_validate_cct_rejects_rgb);
    RUN_TEST(test_validate_switch_accepts_brightness);
    RUN_TEST(test_normalize_clamps_brightness);
    RUN_TEST(test_normalize_zeros_unsupported_rgb_on_dimmer);
    RUN_TEST(test_normalize_cct_zeros_rgb);
    // Transition engine tests
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

// ========================================================================
// Color engine tests
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
    TEST_ASSERT_EQUAL(0, apply_gamma(1));
    TEST_ASSERT_EQUAL(1, apply_gamma(15));
}

// RGB <-> HSV
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
    HsvState h{0, 100, 100};
    Rgb8 rgb = hsv_to_rgb(h);
    TEST_ASSERT_EQUAL(255, rgb.r);
    TEST_ASSERT_EQUAL(0, rgb.g);
    TEST_ASSERT_EQUAL(0, rgb.b);

    h = {120, 100, 100};
    rgb = hsv_to_rgb(h);
    TEST_ASSERT_EQUAL(0, rgb.r);
    TEST_ASSERT_EQUAL(255, rgb.g);
    TEST_ASSERT_EQUAL(0, rgb.b);

    h = {240, 100, 100};
    rgb = hsv_to_rgb(h);
    TEST_ASSERT_EQUAL(0, rgb.r);
    TEST_ASSERT_EQUAL(0, rgb.g);
    TEST_ASSERT_EQUAL(255, rgb.b);

    h = {0, 0, 50};
    rgb = hsv_to_rgb(h);
    TEST_ASSERT_EQUAL(128, rgb.r);
    TEST_ASSERT_EQUAL(128, rgb.g);
    TEST_ASSERT_EQUAL(128, rgb.b);
}

// RGBW
static void test_rgb_to_rgbw(void)
{
    Rgbw8 rgbw = rgb_to_rgbw(255, 0, 0);
    TEST_ASSERT_EQUAL(255, rgbw.r);
    TEST_ASSERT_EQUAL(0, rgbw.g);
    TEST_ASSERT_EQUAL(0, rgbw.b);
    TEST_ASSERT_EQUAL(0, rgbw.w);

    rgbw = rgb_to_rgbw(100, 100, 100);
    TEST_ASSERT_EQUAL(0, rgbw.r);
    TEST_ASSERT_EQUAL(0, rgbw.g);
    TEST_ASSERT_EQUAL(0, rgbw.b);
    TEST_ASSERT_EQUAL(100, rgbw.w);
}

static void test_rgbw_to_rgb(void)
{
    Rgb8 rgb = rgbw_to_rgb(0, 0, 0, 100);
    TEST_ASSERT_EQUAL(100, rgb.r);
    TEST_ASSERT_EQUAL(100, rgb.g);
    TEST_ASSERT_EQUAL(100, rgb.b);

    rgb = rgbw_to_rgb(200, 0, 0, 100);
    TEST_ASSERT_EQUAL(255, rgb.r);
    TEST_ASSERT_EQUAL(100, rgb.g);
    TEST_ASSERT_EQUAL(100, rgb.b);
}

// CCT
static void test_kelvin_to_rgb_warm(void)
{
    Rgb8 rgb = kelvin_to_rgb(1000);
    TEST_ASSERT_TRUE(rgb.r > 100);
    TEST_ASSERT_TRUE(rgb.g < 100);
    TEST_ASSERT_EQUAL(0, rgb.b);
}

static void test_kelvin_to_rgb_cool(void)
{
    Rgb8 rgb = kelvin_to_rgb(10000);
    TEST_ASSERT_TRUE(rgb.r < 255);
    TEST_ASSERT_EQUAL(255, rgb.b);
}

static void test_kelvin_to_rgb_clamp(void)
{
    Rgb8 low = kelvin_to_rgb(100);
    Rgb8 min = kelvin_to_rgb(1000);
    TEST_ASSERT_EQUAL(low.r, min.r);
    TEST_ASSERT_EQUAL(low.g, min.g);
    TEST_ASSERT_EQUAL(low.b, min.b);

    Rgb8 high = kelvin_to_rgb(50000);
    Rgb8 max = kelvin_to_rgb(20000);
    TEST_ASSERT_EQUAL(high.r, max.r);
    TEST_ASSERT_EQUAL(high.g, max.g);
    TEST_ASSERT_EQUAL(high.b, max.b);
}

static void test_rgb_to_kelvin_saturated_returns_zero(void)
{
    uint16_t k = rgb_to_kelvin(255, 0, 0);
    TEST_ASSERT_EQUAL(0, k);
}

// Brightness
static void test_apply_brightness(void)
{
    TEST_ASSERT_EQUAL(0, apply_brightness(100, 0));
    TEST_ASSERT_EQUAL(100, apply_brightness(100, 100));
    TEST_ASSERT_EQUAL(0, apply_brightness(0, 50));

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

// Calibration
static void test_apply_calibration(void)
{
    Rgbw8 in{100, 150, 200, 50};
    ChannelCalibration cal{0.5f, 2.0f, 1.0f, 1.0f};
    Rgbw8 out = apply_calibration(in, cal);

    TEST_ASSERT_EQUAL(50, out.r);
    TEST_ASSERT_EQUAL(255, out.g);
    TEST_ASSERT_EQUAL(200, out.b);
    TEST_ASSERT_EQUAL(50, out.w);
}

// Validation
static void test_validate_switch_rejects_color(void)
{
    LightState s{true, 50, 200, 0, 0, 0, 0};
    TEST_ASSERT_FALSE(validate_state_for_type(s, "switch"));
}

static void test_validate_dimmer_rejects_color(void)
{
    LightState s{true, 50, 0, 100, 0, 0, 0};
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

// Normalize
static void test_normalize_clamps_brightness(void)
{
    LightState s{true, 150, 100, 50, 0, 0, 2700};
    LightState out = normalize_state(s, "dimmer", ChannelCalibration{});
    TEST_ASSERT_EQUAL(100, out.brightness);
}

static void test_normalize_zeros_unsupported_rgb_on_dimmer(void)
{
    LightState s{true, 50, 100, 50, 0, 0, 0};
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

// Transition engine tests
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
