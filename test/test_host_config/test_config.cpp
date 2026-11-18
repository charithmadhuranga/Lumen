// Host tests for the config manager: load pipeline (read → parse → schema
// check → migrate → validate → defaults), transactional save, and
// config_changed notification. Fault injection via corrupt/unparseable
// documents and unknown schema versions.
#include <cstring>
#include <string>

#include <unity.h>

#include "lumen/core/config_manager.hpp"
#include "lumen/core/event.hpp"
#include "lumen/core/event_bus.hpp"
#include "lumen/core/json.hpp"
#include "lumen/core/storage.hpp"

using lumen::ConfigManager;
using lumen::ConfigStatus;
using lumen::create_cjson_adapter;
using lumen::create_posix_fs;
using lumen::DocConfig;
using lumen::Event;
using lumen::EventBus;
using lumen::EventId;
using lumen::EventPriority;
using lumen::IJson;
using lumen::IStorage;
using lumen::StorageError;
using lumen::SubscribeOptions;

namespace
{

// Minimal host ports (single-threaded host tests).
struct NoopLock final : lumen::ILock
{
    void lock() override {}
    void unlock() override {}
};

struct FakeClock final : lumen::IClock
{
    uint32_t now = 0;
    uint32_t ms() override { return now; }
};

std::string g_root;
NoopLock g_lock;
FakeClock g_clock;
lumen::Event g_pool[64];

IStorage *g_storage = nullptr;
IJson *g_json = nullptr;
EventBus *g_bus = nullptr;

struct Recorder
{
    std::vector<Event> got;
};

void record_event(const Event &event, void *user)
{
    static_cast<Recorder *>(user)->got.push_back(event);
}

extern "C" void setUp(void)
{
    g_root = std::string(LUMEN_REPO_ROOT) + "/.test_config_tmp";
    std::string cmd = "rm -rf \"";
    cmd += g_root + "\" && mkdir -p \"";
    cmd += g_root + "\"";
    (void)std::system(cmd.c_str());

    g_storage = create_posix_fs(g_root);
    g_json = create_cjson_adapter();
    g_bus = new EventBus(g_lock, g_clock, g_pool, 64);
}

extern "C" void tearDown(void)
{
    delete g_bus;
    delete g_json;
    delete g_storage;
    std::string cmd = "rm -rf \"";
    cmd += g_root + "\"";
    (void)std::system(cmd.c_str());
}

// --- test document: system.json, schema v1 --------------------------------
const char *kSystemPath = "system.json";
const uint16_t kSystemLatest = 1;

std::string system_default()
{
    return "{\"schema_version\":1,\"wifi_ssid\":null}";
}

// v0 → v1: add wifi.ssid, bump schema_version.
bool system_migrate(std::string &doc, uint16_t from, uint16_t to, IJson &json)
{
    (void)from;
    lumen::JsonDoc *d = json.parse(doc);
    if (d == nullptr) {
        return false;
    }
    json.add_object(d, "wifi");
    lumen::JsonDoc *wifi = json.get_object(d, "wifi");
    if (wifi == nullptr) {
        json.free(d);
        return false;
    }
    json.set_string(wifi, "ssid", "lumen");
    json.set_int(d, "schema_version", (int32_t)to);
    doc = json.stringify(d);
    json.free(d);
    return !doc.empty();
}

bool system_validate(const std::string &doc, IJson &json)
{
    lumen::JsonDoc *d = json.parse(doc);
    if (d == nullptr) {
        return false;
    }
    int32_t v = 0;
    bool ok = (json.get_int(d, "schema_version", v) == IJson::JsonResult::kOk);
    json.free(d);
    return ok;
}

ConfigManager make_config()
{
    ConfigManager cfg(*g_storage, *g_json, *g_bus);
    DocConfig system;
    system.path = kSystemPath;
    system.latest_schema_version = kSystemLatest;
    system.migrate = system_migrate;
    system.validate = system_validate;
    system.default_doc = system_default;
    cfg.register_document(system);
    return cfg;
}

Recorder &subscribe_config_changed(lumen::SubscriptionId &out_id)
{
    static Recorder rec;
    SubscribeOptions opts;
    opts.handler = record_event;
    opts.user = &rec;
    opts.capacity = 4;
    opts.id = EventId::ConfigChanged;
    opts.min_priority = EventPriority::Telemetry;
    out_id = g_bus->subscribe(opts);
    return rec;
}

} // namespace

// ---------------------------------------------------------------------------
static void test_register_and_duplicate_rejection(void)
{
    ConfigManager cfg(*g_storage, *g_json, *g_bus);
    DocConfig a;
    a.path = "a.json";
    a.latest_schema_version = 1;
    TEST_ASSERT_TRUE(cfg.register_document(a));

    DocConfig b;
    b.path = "a.json"; // duplicate
    b.latest_schema_version = 1;
    TEST_ASSERT_FALSE(cfg.register_document(b));
}

static void test_load_missing_uses_defaults(void)
{
    ConfigManager cfg = make_config();
    std::string out;
    ConfigStatus st = cfg.load_document(kSystemPath, out);
    TEST_ASSERT_EQUAL(ConfigStatus::kNotFound, st);
    TEST_ASSERT_EQUAL_STRING("{\"schema_version\":1,\"wifi_ssid\":null}",
                             out.c_str());
}

static void test_load_valid_document(void)
{
    ConfigManager cfg = make_config();
    TEST_ASSERT_EQUAL(StorageError::kOk,
                      g_storage->write(kSystemPath,
                                       "{\"schema_version\":1,\"wifi_ssid\":null}"));

    std::string out;
    ConfigStatus st = cfg.load_document(kSystemPath, out);
    TEST_ASSERT_EQUAL(ConfigStatus::kOk, st);
    lumen::JsonDoc *d = g_json->parse(out);
    TEST_ASSERT_TRUE(d != nullptr);
    int32_t v = 0;
    TEST_ASSERT_EQUAL(IJson::JsonResult::kOk,
                      g_json->get_int(d, "schema_version", v));
    TEST_ASSERT_EQUAL_INT32(1, v);
    g_json->free(d);
}

static void test_load_unparseable_uses_defaults(void)
{
    ConfigManager cfg = make_config();
    TEST_ASSERT_EQUAL(StorageError::kOk,
                      g_storage->write(kSystemPath, "{not json"));

    std::string out;
    ConfigStatus st = cfg.load_document(kSystemPath, out);
    TEST_ASSERT_EQUAL(ConfigStatus::kCorrupt, st);
    TEST_ASSERT_EQUAL_STRING("{\"schema_version\":1,\"wifi_ssid\":null}",
                             out.c_str());
}

static void test_load_unknown_schema_rejected_file_intact(void)
{
    ConfigManager cfg = make_config();
    TEST_ASSERT_EQUAL(StorageError::kOk,
                      g_storage->write(kSystemPath,
                                       "{\"schema_version\":2,\"wifi_ssid\":\"x\"}"));

    std::string out;
    ConfigStatus st = cfg.load_document(kSystemPath, out);
    TEST_ASSERT_EQUAL(ConfigStatus::kRejected, st);
    TEST_ASSERT_TRUE(out.empty());
    std::string raw;
    TEST_ASSERT_EQUAL(StorageError::kOk, g_storage->read(kSystemPath, raw));
    TEST_ASSERT_EQUAL_STRING("{\"schema_version\":2,\"wifi_ssid\":\"x\"}",
                             raw.c_str());
}

static void test_load_older_schema_migrated(void)
{
    ConfigManager cfg = make_config();
    TEST_ASSERT_EQUAL(StorageError::kOk,
                      g_storage->write(kSystemPath, "{\"schema_version\":0}"));

    std::string out;
    ConfigStatus st = cfg.load_document(kSystemPath, out);
    TEST_ASSERT_EQUAL(ConfigStatus::kOk, st);

    lumen::JsonDoc *d = g_json->parse(out);
    TEST_ASSERT_TRUE(d != nullptr);
    int32_t v = 0;
    TEST_ASSERT_EQUAL(IJson::JsonResult::kOk,
                      g_json->get_int(d, "schema_version", v));
    TEST_ASSERT_EQUAL_INT32(1, v);
    lumen::JsonDoc *wifi = g_json->get_object(d, "wifi");
    TEST_ASSERT_TRUE(wifi != nullptr);
    std::string ssid;
    TEST_ASSERT_EQUAL(IJson::JsonResult::kOk,
                      g_json->get_string(wifi, "ssid", ssid));
    TEST_ASSERT_EQUAL_STRING("lumen", ssid.c_str());
    g_json->free(d);
}

static void test_save_valid_document_posts_event(void)
{
    ConfigManager cfg = make_config();
    lumen::SubscriptionId sub = 0;
    Recorder &rec = subscribe_config_changed(sub);

    ConfigStatus st =
        cfg.save_document(kSystemPath, "{\"schema_version\":1,\"wifi_ssid\":null}");
    TEST_ASSERT_EQUAL(ConfigStatus::kOk, st);

    TEST_ASSERT_EQUAL_size_t(1, g_bus->poll(sub, 4));
    TEST_ASSERT_EQUAL_size_t(1, rec.got.size());
    TEST_ASSERT_EQUAL_INT(static_cast<int>(EventId::ConfigChanged),
                          static_cast<int>(rec.got[0].id));

    // File written; verify parsed fields (key order is not meaningful).
    std::string raw;
    TEST_ASSERT_EQUAL(StorageError::kOk, g_storage->read(kSystemPath, raw));
    lumen::JsonDoc *rd = g_json->parse(raw);
    TEST_ASSERT_TRUE(rd != nullptr);
    int32_t v = 0;
    TEST_ASSERT_EQUAL(IJson::JsonResult::kOk,
                      g_json->get_int(rd, "schema_version", v));
    TEST_ASSERT_EQUAL_INT32(1, v);
    TEST_ASSERT_TRUE(g_json->has(rd, "wifi_ssid"));
    g_json->free(rd);
}

static void test_save_invalid_document_rejected(void)
{
    ConfigManager cfg = make_config();

    ConfigStatus st = cfg.save_document(kSystemPath, "{not json");
    TEST_ASSERT_EQUAL(ConfigStatus::kInvalid, st);
    TEST_ASSERT_FALSE(g_storage->exists(kSystemPath));
}

static void test_save_unknown_schema_rejected(void)
{
    ConfigManager cfg = make_config();

    ConfigStatus st = cfg.save_document(kSystemPath, "{\"schema_version\":99}");
    TEST_ASSERT_EQUAL(ConfigStatus::kRejected, st);
    TEST_ASSERT_FALSE(g_storage->exists(kSystemPath));
}

static void test_save_stamps_schema_version(void)
{
    ConfigManager cfg = make_config();
    ConfigStatus st = cfg.save_document(kSystemPath, "{\"wifi_ssid\":null}");
    TEST_ASSERT_EQUAL(ConfigStatus::kOk, st);

    std::string raw;
    TEST_ASSERT_EQUAL(StorageError::kOk, g_storage->read(kSystemPath, raw));
    lumen::JsonDoc *d = g_json->parse(raw);
    TEST_ASSERT_TRUE(d != nullptr);
    int32_t v = 0;
    TEST_ASSERT_EQUAL(IJson::JsonResult::kOk,
                      g_json->get_int(d, "schema_version", v));
    TEST_ASSERT_EQUAL_INT32(1, v);
    g_json->free(d);
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_register_and_duplicate_rejection);
    RUN_TEST(test_load_missing_uses_defaults);
    RUN_TEST(test_load_valid_document);
    RUN_TEST(test_load_unparseable_uses_defaults);
    RUN_TEST(test_load_unknown_schema_rejected_file_intact);
    RUN_TEST(test_load_older_schema_migrated);
    RUN_TEST(test_save_valid_document_posts_event);
    RUN_TEST(test_save_invalid_document_rejected);
    RUN_TEST(test_save_unknown_schema_rejected);
    RUN_TEST(test_save_stamps_schema_version);
    return UNITY_END();
}
