// Host tests: configuration backup/restore (transactional, hostile input)
// and OTA image header pre-validation.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

#include <unity.h>

#include "core/cJSON.h"
#include "lumen/services/backup.h"
#include "lumen/services/ota_image.h"

using namespace lumen;

namespace
{

std::string g_root;

void write(const std::string &rel, const std::string &data)
{
    const std::size_t slash = rel.find('/');
    if (slash != std::string::npos) mkdir((g_root + "/" + rel.substr(0, slash)).c_str(), 0755);
    FILE *f = std::fopen((g_root + "/" + rel).c_str(), "wb");
    TEST_ASSERT_NOT_NULL(f);
    std::fwrite(data.data(), 1, data.size(), f);
    std::fclose(f);
}

std::string read(const std::string &rel)
{
    FILE *f = std::fopen((g_root + "/" + rel).c_str(), "rb");
    if (f == nullptr) return "<missing>";
    std::string s;
    char b[512];
    std::size_t n;
    while ((n = std::fread(b, 1, sizeof(b), f)) > 0) s.append(b, n);
    std::fclose(f);
    return s;
}

bool exists(const std::string &p)
{
    struct stat st{};
    return stat(p.c_str(), &st) == 0;
}

void seed()
{
    write("lights.json", R"({"lights":[{"id":"a"}]})");
    write("scenes.json", R"({"scenes":[]})");
    write("lights.json.bak", R"({"old":true})");
    write("automations/porch.json", R"({"id":"porch"})");
}

} // namespace

extern "C" void setUp(void)
{
    char tmpl[] = "/tmp/lumen_backup_XXXXXX";
    TEST_ASSERT_NOT_NULL(mkdtemp(tmpl));
    g_root = std::string(tmpl) + "/config";
    TEST_ASSERT_EQUAL(0, mkdir(g_root.c_str(), 0755));
    seed();
}

extern "C" void tearDown(void)
{
    const std::string cmd = "rm -rf '" + g_root.substr(0, g_root.rfind('/')) + "'";
    TEST_ASSERT_EQUAL(0, std::system(cmd.c_str()));
}

static void test_create_collects_documents_only(void)
{
    const std::string b = backup::create(g_root, "1.2.3", "kc868", 1700000000, {});
    cJSON *o = cJSON_Parse(b.c_str());
    TEST_ASSERT_NOT_NULL(o);
    TEST_ASSERT_EQUAL_STRING("lumen-backup", cJSON_GetObjectItem(o, "format")->valuestring);
    const cJSON *docs = cJSON_GetObjectItem(o, "documents");
    TEST_ASSERT_NOT_NULL(cJSON_GetObjectItem(docs, "lights.json"));
    TEST_ASSERT_NOT_NULL(cJSON_GetObjectItem(docs, "automations/porch.json"));
    TEST_ASSERT_NULL(cJSON_GetObjectItem(docs, "lights.json.bak")); // storage generations never exported
    TEST_ASSERT_EQUAL(3, cJSON_GetArraySize(docs));
    cJSON_Delete(o);

    const std::string partial = backup::create(g_root, "1.2.3", "kc868", 0, {"automations"});
    o = cJSON_Parse(partial.c_str());
    docs = cJSON_GetObjectItem(o, "documents");
    TEST_ASSERT_EQUAL(1, cJSON_GetArraySize(docs));
    TEST_ASSERT_NOT_NULL(cJSON_GetObjectItem(docs, "automations/porch.json"));
    cJSON_Delete(o);
}

static void test_full_restore_replaces_exactly(void)
{
    const std::string b = R"({"format":"lumen-backup","version":1,"documents":{"lights.json":{"lights":[{"id":"b"}]},"rooms.json":{"rooms":[]}}})";
    std::string err;
    std::vector<std::string> done;
    TEST_ASSERT_TRUE_MESSAGE(backup::restore(g_root, b, true, err, &done), err.c_str());
    TEST_ASSERT_EQUAL(2, done.size());
    TEST_ASSERT_EQUAL_STRING(R"({"lights":[{"id":"b"}]})", read("lights.json").c_str());
    TEST_ASSERT_EQUAL_STRING(R"({"rooms":[]})", read("rooms.json").c_str());
    TEST_ASSERT_EQUAL_STRING("<missing>", read("scenes.json").c_str());           // not in backup
    TEST_ASSERT_EQUAL_STRING("<missing>", read("automations/porch.json").c_str());
    TEST_ASSERT_FALSE(exists(g_root + ".old"));
    TEST_ASSERT_FALSE(exists(g_root + ".restore"));
}

static void test_round_trip_and_header_order(void)
{
    const std::string b = backup::create(g_root, "1.2.3", "kc868", 5, {});
    write("lights.json", R"({"lights":[]})");
    std::string err;
    TEST_ASSERT_TRUE_MESSAGE(backup::restore(g_root, b, true, err), err.c_str());
    TEST_ASSERT_EQUAL_STRING(R"({"lights":[{"id":"a"}]})", read("lights.json").c_str());
    TEST_ASSERT_EQUAL_STRING(R"({"id":"porch"})", read("automations/porch.json").c_str());
    // Header fields may come after the documents; strings with braces/escapes survive.
    const std::string late = R"({"documents":{"scenes.json":{"n":"a}\"b{"}},"format":"lumen-backup","version":1})";
    TEST_ASSERT_TRUE_MESSAGE(backup::restore(g_root, late, false, err), err.c_str());
    TEST_ASSERT_EQUAL_STRING(R"({"n":"a}\"b{"})", read("scenes.json").c_str());
}

static void test_partial_restore_keeps_other_documents(void)
{
    const std::string b = R"({"format":"lumen-backup","version":1,"documents":{"automations/porch.json":{"id":"porch","v":2}}})";
    std::string err;
    TEST_ASSERT_TRUE_MESSAGE(backup::restore(g_root, b, false, err), err.c_str());
    TEST_ASSERT_EQUAL_STRING(R"({"id":"porch","v":2})", read("automations/porch.json").c_str());
    TEST_ASSERT_EQUAL_STRING(R"({"lights":[{"id":"a"}]})", read("lights.json").c_str());
    TEST_ASSERT_EQUAL_STRING(R"({"scenes":[]})", read("scenes.json").c_str());
}

static void test_hostile_backups_change_nothing(void)
{
    const char *bad[] = {
        "not json",
        R"({"format":"other","version":1,"documents":{"a.json":{}}})",
        R"({"format":"lumen-backup","version":2,"documents":{"a.json":{}}})",
        R"({"format":"lumen-backup","version":1,"documents":{}})",
        R"({"format":"lumen-backup","version":1,"documents":{"../escape.json":{}}})",
        R"({"format":"lumen-backup","version":1,"documents":{"/etc/x.json":{}}})",
        R"({"format":"lumen-backup","version":1,"documents":{"a/b/c.json":{}}})",
        R"({"format":"lumen-backup","version":1,"documents":{"lights.json.bak":{}}})",
        R"({"format":"lumen-backup","version":1,"documents":{"x y.json":{}}})",
        R"({"format":"lumen-backup","version":1,"documents":{"lights.json":"string"}})",
        R"({"format":"lumen-backup","version":1,"documents":{"ok.json":{},"bad/../../x.json":{}}})",
        R"({"documents":{"a.json":{}},"format":"lumen-backup","version":1)",         // truncated
        R"({"documents":{"a.json":{"x":1}}})",                                       // no header
        R"({"format":"lumen-backup","version":1,"documents":{"a.json":{"x":}}})",    // invalid document JSON
        R"({"format":"lumen-backup","version":1,"documents":{"a.json":{}}} trailing)",
    };
    for (const char *b : bad) {
        std::string err;
        TEST_ASSERT_FALSE_MESSAGE(backup::restore(g_root, b, true, err), b);
        TEST_ASSERT_TRUE(err.size() > 0);
        TEST_ASSERT_EQUAL_STRING(R"({"lights":[{"id":"a"}]})", read("lights.json").c_str());
        TEST_ASSERT_EQUAL_STRING(R"({"scenes":[]})", read("scenes.json").c_str());
        TEST_ASSERT_FALSE(exists(g_root + ".restore"));
    }
    TEST_ASSERT_FALSE(exists(g_root.substr(0, g_root.rfind('/')) + "/escape.json"));
    std::string big = R"({"format":"lumen-backup","version":1,"documents":{"a.json":{"x":")";
    big += std::string(40 * 1024, 'a') + "\"}}}"; // one document over 32 KB
    std::string err;
    TEST_ASSERT_FALSE(backup::restore(g_root, big, true, err));
}

static void test_recover_interrupted_swap(void)
{
    // Power cut after "config -> config.old", before "staging -> config".
    TEST_ASSERT_EQUAL(0, rename(g_root.c_str(), (g_root + ".old").c_str()));
    mkdir((g_root + ".restore").c_str(), 0755);
    TEST_ASSERT_TRUE(backup::recover_interrupted(g_root));
    TEST_ASSERT_EQUAL_STRING(R"({"lights":[{"id":"a"}]})", read("lights.json").c_str());
    TEST_ASSERT_FALSE(exists(g_root + ".restore"));
    TEST_ASSERT_FALSE(backup::recover_interrupted(g_root)); // idempotent
}

static void test_document_path_rules(void)
{
    TEST_ASSERT_TRUE(backup::valid_document_path("lights.json"));
    TEST_ASSERT_TRUE(backup::valid_document_path("automations/a-b_1.json"));
    for (const char *p : {"", "lights", "lights.txt", "../a.json", "a/../b.json", "/a.json", "a//b.json", "a/b/c.json", "a b.json", "a\\b.json"}) {
        TEST_ASSERT_FALSE_MESSAGE(backup::valid_document_path(p), p);
    }
}

// ---------------------------------------------------------------- OTA header
static std::vector<uint8_t> image(uint16_t chip, const char *version, const char *project)
{
    std::vector<uint8_t> d(512, 0);
    d[0] = 0xE9;
    d[12] = static_cast<uint8_t>(chip & 0xFF);
    d[13] = static_cast<uint8_t>(chip >> 8);
    const uint32_t magic = ota::kAppDescMagic;
    std::memcpy(&d[32], &magic, 4); // little-endian host
    std::strncpy(reinterpret_cast<char *>(&d[48]), version, 31);
    std::strncpy(reinterpret_cast<char *>(&d[80]), project, 31);
    return d;
}

static void test_ota_header_checks(void)
{
    ota::ImageInfo info;
    std::string err;
    auto ok = image(0, "0.2.0", "lumen-firmware");
    TEST_ASSERT_TRUE_MESSAGE(ota::check_header(ok.data(), ok.size(), 0, "lumen-firmware", info, err), err.c_str());
    TEST_ASSERT_EQUAL_STRING("0.2.0", info.version.c_str());

    auto s3 = image(9, "0.2.0", "lumen-firmware");
    TEST_ASSERT_FALSE(ota::check_header(s3.data(), s3.size(), 0, "lumen-firmware", info, err));
    TEST_ASSERT_NOT_NULL(std::strstr(err.c_str(), "ESP32-S3"));

    auto other = image(0, "1.0", "some-other-app");
    TEST_ASSERT_FALSE(ota::check_header(other.data(), other.size(), 0, "lumen-firmware", info, err));

    auto nomagic = ok;
    nomagic[0] = 0x7F; // e.g. an ELF file
    TEST_ASSERT_FALSE(ota::check_header(nomagic.data(), nomagic.size(), 0, "lumen-firmware", info, err));

    auto nodesc = ok;
    nodesc[32] = 0;
    TEST_ASSERT_FALSE(ota::check_header(nodesc.data(), nodesc.size(), 0, "lumen-firmware", info, err));

    auto nover = image(0, "", "lumen-firmware");
    TEST_ASSERT_FALSE(ota::check_header(nover.data(), nover.size(), 0, "lumen-firmware", info, err));

    TEST_ASSERT_FALSE(ota::check_header(ok.data(), 10, 0, "lumen-firmware", info, err));
    TEST_ASSERT_FALSE(ota::check_header(nullptr, 0, 0, "lumen-firmware", info, err));
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_create_collects_documents_only);
    RUN_TEST(test_full_restore_replaces_exactly);
    RUN_TEST(test_round_trip_and_header_order);
    RUN_TEST(test_partial_restore_keeps_other_documents);
    RUN_TEST(test_hostile_backups_change_nothing);
    RUN_TEST(test_recover_interrupted_swap);
    RUN_TEST(test_document_path_rules);
    RUN_TEST(test_ota_header_checks);
    return UNITY_END();
}
