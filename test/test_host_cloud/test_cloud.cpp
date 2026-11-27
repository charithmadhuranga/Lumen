// Host tests: controller cloud link (settings, pairing messages, LAN URL rule,
// ticket clock) and keeping the pairing out of backups (Lumen Pro P5).
#include <cstdio>
#include <cstdlib>
#include <string>
#include <sys/stat.h>

#include <unity.h>

#include "core/cJSON.h"
#include "lumen/core/crypto.hpp"
#include "lumen/services/backup.h"
#include "lumen/services/cloud_link.hpp"

using namespace lumen;

extern "C" void setUp(void) {}
extern "C" void tearDown(void) {}

namespace
{

const char *kDev = "0193f6c2-7a10-7b44-9d92-001122334455";
const char *kGw = "0193f6c2-7a10-7c00-8a00-00000000aa01";

std::string read_file(const std::string &path)
{
    FILE *f = std::fopen(path.c_str(), "rb");
    if (f == nullptr) return "";
    std::string s;
    char buf[4096];
    std::size_t n;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) s.append(buf, n);
    std::fclose(f);
    return s;
}

void write_file(const std::string &path, const std::string &text)
{
    FILE *f = std::fopen(path.c_str(), "wb");
    TEST_ASSERT_NOT_NULL(f);
    std::fwrite(text.data(), 1, text.size(), f);
    std::fclose(f);
}

std::string fixture(const char *rel)
{
    return read_file(std::string(LUMEN_REPO_ROOT) + "/protocol/fixtures/" + rel);
}

} // namespace

// --- settings -------------------------------------------------------------------------------

void test_settings_round_trip_and_defaults()
{
    cloud::Settings s;
    s.paired = true;
    s.device_id = kDev;
    s.gateway_id = kGw;
    s.gateway_url = "http://192.168.1.20:8780";
    s.ticket_epoch = 3;
    s.paired_at = 1795780800;
    cloud::Settings back;
    std::string err;
    TEST_ASSERT_TRUE_MESSAGE(cloud::parse_settings(cloud::to_json(s), back, err), err.c_str());
    TEST_ASSERT_TRUE(back.paired);
    TEST_ASSERT_EQUAL_STRING(kDev, back.device_id.c_str());
    TEST_ASSERT_EQUAL_STRING("http://192.168.1.20:8780", back.gateway_url.c_str());
    TEST_ASSERT_EQUAL_UINT32(3, back.ticket_epoch);
    TEST_ASSERT_EQUAL_INT64(1795780800, back.paired_at);

    TEST_ASSERT_TRUE(cloud::parse_settings(R"({"schema_version":1,"mode":"off"})", back, err));
    TEST_ASSERT_FALSE(back.paired);
    TEST_ASSERT_EQUAL_STRING(R"({"schema_version":1,"mode":"off"})", cloud::to_json(cloud::Settings{}).c_str());
}

void test_settings_reject_invalid()
{
    const char *bad[] = {
        "not json",
        "[]",
        R"({"schema_version":2,"mode":"off"})",
        R"({"schema_version":1,"mode":"direct"})",
        R"({"schema_version":1,"mode":"gateway"})",
        R"({"schema_version":1,"mode":"gateway","deviceId":"x","gatewayId":"0193f6c2-7a10-7c00-8a00-00000000aa01","gatewayUrl":"http://10.0.0.2","ticketEpoch":0})",
        R"({"schema_version":1,"mode":"gateway","deviceId":"0193f6c2-7a10-7b44-9d92-001122334455","gatewayId":"0193f6c2-7a10-7c00-8a00-00000000aa01","gatewayUrl":"http://8.8.8.8","ticketEpoch":0})",
        R"({"schema_version":1,"mode":"gateway","deviceId":"0193f6c2-7a10-7b44-9d92-001122334455","gatewayId":"0193f6c2-7a10-7c00-8a00-00000000aa01","gatewayUrl":"http://10.0.0.2","ticketEpoch":-1})",
    };
    for (const char *b : bad) {
        cloud::Settings s;
        std::string err;
        TEST_ASSERT_FALSE_MESSAGE(cloud::parse_settings(b, s, err), b);
        TEST_ASSERT_FALSE(s.paired);
        TEST_ASSERT_FALSE(err.empty());
    }
}

// --- input rules ----------------------------------------------------------------------------------

void test_lan_gateway_url()
{
    const char *ok[] = {"http://10.0.0.1", "http://172.16.0.5:8780", "http://172.31.255.254", "http://192.168.1.20:80",
                        "http://169.254.10.10"};
    const char *bad[] = {"https://192.168.1.20", "http://8.8.8.8", "http://172.32.0.1", "http://192.169.1.1",
                         "http://192.168.1.20/local", "http://lumen-gw.local", "http://192.168.1", "http://192.168.1.256",
                         "http://192.168.01.2", "http://192.168.1.20:0", "http://192.168.1.20:65536", "http://user@10.0.0.1",
                         "ftp://10.0.0.1", "http://10.0.0.1:"};
    for (const char *u : ok) TEST_ASSERT_TRUE_MESSAGE(cloud::is_lan_gateway_url(u), u);
    for (const char *u : bad) TEST_ASSERT_FALSE_MESSAGE(cloud::is_lan_gateway_url(u), u);
}

void test_claim_token_and_uuid()
{
    TEST_ASSERT_TRUE(cloud::is_claim_token("lct_" + std::string(43, 'A')));
    TEST_ASSERT_FALSE(cloud::is_claim_token("lct_" + std::string(42, 'A')));
    TEST_ASSERT_FALSE(cloud::is_claim_token("lrt_" + std::string(43, 'A')));
    TEST_ASSERT_FALSE(cloud::is_claim_token("lct_" + std::string(42, 'A') + "+"));
    TEST_ASSERT_TRUE(cloud::is_uuid(kDev));
    TEST_ASSERT_FALSE(cloud::is_uuid("0193F6C2-7A10-7B44-9D92-001122334455"));
    TEST_ASSERT_FALSE(cloud::is_uuid("0193f6c2-7a10-0b44-9d92-001122334455")); // version 0
    TEST_ASSERT_FALSE(cloud::is_uuid("0193f6c2-7a10-7b44-cd92-001122334455")); // variant
}

// --- pairing messages: protocol fixtures ----------------------------------------------------------

void test_register_request_matches_protocol_fixture()
{
    cloud::Hardware hw{"kc868-ap", "kincony_kc868_ap", "a0:b7:65:12:07:98", "esp32"};
    const std::string got = cloud::register_request("lct_" + std::string(43, 'A'), hw, "1.1.0", "lumen-0798",
                                                    "http://192.168.1.42");
    cJSON *a = cJSON_Parse(got.c_str());
    cJSON *b = cJSON_Parse(fixture("register-request/valid/kc868.json").c_str());
    TEST_ASSERT_NOT_NULL(b);
    TEST_ASSERT_TRUE_MESSAGE(cJSON_Compare(a, b, true), got.c_str());
    cJSON_Delete(a);
    cJSON_Delete(b);
}

void test_register_response()
{
    // The protocol fixture uses a dummy key ("kkkk..."); use a real 32-byte key here.
    uint8_t key[32];
    for (int i = 0; i < 32; ++i) key[i] = uint8_t(i * 7);
    const std::string k = crypto::b64url_encode(key, 32);
    const std::string body = std::string(R"({"success":true,"data":{"v":1,"deviceId":")") + kDev + R"(","localKey":")" + k +
                             R"(","ticketEpoch":4,"gatewayId":")" + kGw + R"("},"error":null})";
    cloud::Registration r;
    std::string err;
    TEST_ASSERT_TRUE_MESSAGE(cloud::parse_register_response(body, r, err), err.c_str());
    TEST_ASSERT_EQUAL_STRING(kDev, r.device_id.c_str());
    TEST_ASSERT_EQUAL_STRING(kGw, r.gateway_id.c_str());
    TEST_ASSERT_EQUAL_UINT32(4, r.ticket_epoch);
    TEST_ASSERT_EQUAL_MEMORY(key, r.local_key.data(), 32);

    // The shape of protocol/fixtures/register-response/valid/child.json, wrapped in the envelope.
    const std::string child = fixture("register-response/valid/child.json");
    TEST_ASSERT_FALSE(child.empty());
    const std::string wrapped = std::string(R"({"success":true,"data":)") + child + "}";
    TEST_ASSERT_TRUE_MESSAGE(cloud::parse_register_response(wrapped, r, err), err.c_str());
}

void test_register_response_rejects()
{
    const std::string key = std::string(43, 'A');
    const std::string good = std::string(R"("deviceId":")") + kDev + R"(","gatewayId":")" + kGw + R"(")";
    const std::string bad[] = {
        "",
        "<html>",
        R"({"success":false,"data":null,"error":{"code":"INVALID_CLAIM","message":"The claim code is invalid"}})",
        R"({"success":true,"data":null})",
        R"({"success":true,"data":{"v":2,)" + good + R"(,"localKey":")" + key + R"(","ticketEpoch":0}})",
        R"({"success":true,"data":{"v":1,)" + good + R"(,"localKey":"short","ticketEpoch":0}})",
        R"({"success":true,"data":{"v":1,)" + good + R"(,"localKey":")" + std::string(42, 'A') + R"(+","ticketEpoch":0}})",
        R"({"success":true,"data":{"v":1,)" + good + R"(,"localKey":")" + key + R"(","ticketEpoch":1.5}})",
        R"({"success":true,"data":{"v":1,"deviceId":"x","gatewayId":"y","localKey":")" + key + R"(","ticketEpoch":0}})",
    };
    for (const auto &b : bad) {
        cloud::Registration r;
        std::string err;
        TEST_ASSERT_FALSE_MESSAGE(cloud::parse_register_response(b, r, err), b.c_str());
        TEST_ASSERT_FALSE(err.empty());
        TEST_ASSERT_TRUE(r.device_id.empty());
    }
    cloud::Registration r;
    std::string err;
    cloud::parse_register_response(bad[2], r, err);
    TEST_ASSERT_NOT_EQUAL(std::string::npos, err.find("The claim code is invalid"));
}

void test_ticket_clock()
{
    TEST_ASSERT_EQUAL_INT64(2000, cloud::ticket_clock(2000, true, 1000));
    TEST_ASSERT_EQUAL_INT64(1000, cloud::ticket_clock(5, false, 1000)); // clock not set after a reboot
    TEST_ASSERT_EQUAL_INT64(1000, cloud::ticket_clock(900, true, 1000)); // never moves backwards
}

// --- backups never carry the pairing ----------------------------------------------------------------

void test_backup_excludes_and_restore_keeps_cloud_json()
{
    const std::string root = std::string(LUMEN_REPO_ROOT) + "/.test_cloud_tmp";
    std::system(("rm -rf '" + root + "' '" + root + ".restore' '" + root + ".old'").c_str());
    mkdir(root.c_str(), 0755);
    write_file(root + "/lights.json", R"({"schema_version":1,"lights":[]})");
    write_file(root + "/cloud.json", R"({"schema_version":1,"mode":"gateway"})");
    const std::string b = backup::create(root, "1.1.0", "kincony_kc868_ap", 1795780800, {});
    TEST_ASSERT_NOT_EQUAL(std::string::npos, b.find("lights.json"));
    TEST_ASSERT_EQUAL(std::string::npos, b.find("cloud.json"));

    // A backup that carries cloud.json is refused.
    std::string err;
    const std::string sneaky =
        R"({"format":"lumen-backup","version":1,"documents":{"lights.json":{"a":1},"cloud.json":{"schema_version":1,"mode":"off"}}})";
    TEST_ASSERT_FALSE(backup::restore(root, sneaky, true, err));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, err.find("cloud.json"));

    // A full restore replaces the configuration but keeps this controller's pairing.
    TEST_ASSERT_TRUE_MESSAGE(backup::restore(root, b, true, err), err.c_str());
    TEST_ASSERT_EQUAL_STRING(R"({"schema_version":1,"mode":"gateway"})", read_file(root + "/cloud.json").c_str());
    TEST_ASSERT_FALSE(read_file(root + "/lights.json").empty());
    std::system(("rm -rf '" + root + "'").c_str());
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_settings_round_trip_and_defaults);
    RUN_TEST(test_settings_reject_invalid);
    RUN_TEST(test_lan_gateway_url);
    RUN_TEST(test_claim_token_and_uuid);
    RUN_TEST(test_register_request_matches_protocol_fixture);
    RUN_TEST(test_register_response);
    RUN_TEST(test_register_response_rejects);
    RUN_TEST(test_ticket_clock);
    RUN_TEST(test_backup_excludes_and_restore_keeps_cloud_json);
    return UNITY_END();
}
