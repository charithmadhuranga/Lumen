// Host tests for diagnostics: log category names, secret redaction, and
// snapshot assembly (all host-testable — no FreeRTOS).
#include <cstring>
#include <string>

#include <unity.h>

#include "lumen/core/diagnostics.hpp"

using lumen::LogCategory;
using lumen::Snapshot;
using lumen::category_name;
using lumen::mask_secret;
using lumen::redact_secret;
using lumen::take_snapshot;

namespace
{

extern "C" void setUp(void) {}
extern "C" void tearDown(void) {}

} // namespace

// ---------------------------------------------------------------------------
static void test_category_names(void)
{
    TEST_ASSERT_EQUAL_STRING("SYSTEM", category_name(LogCategory::kSystem));
    TEST_ASSERT_EQUAL_STRING("HARDWARE", category_name(LogCategory::kHardware));
    TEST_ASSERT_EQUAL_STRING("LIGHTING",
                             category_name(LogCategory::kLighting));
    TEST_ASSERT_EQUAL_STRING("AUTOMATION",
                             category_name(LogCategory::kAutomation));
    TEST_ASSERT_EQUAL_STRING("NETWORK", category_name(LogCategory::kNetwork));
    TEST_ASSERT_EQUAL_STRING("MQTT", category_name(LogCategory::kMqtt));
    TEST_ASSERT_EQUAL_STRING("ESP_NOW", category_name(LogCategory::kEspNow));
    TEST_ASSERT_EQUAL_STRING("WEB", category_name(LogCategory::kWeb));
    TEST_ASSERT_EQUAL_STRING("OTA", category_name(LogCategory::kOta));
    TEST_ASSERT_EQUAL_STRING("SECURITY", category_name(LogCategory::kSecurity));
    TEST_ASSERT_EQUAL_STRING("UNKNOWN",
                             category_name(static_cast<LogCategory>(99)));
}

static void test_redact_secret(void)
{
    TEST_ASSERT_EQUAL_STRING("", redact_secret("").c_str());
    TEST_ASSERT_EQUAL_STRING("[REDACTED]", redact_secret("s3cr3t").c_str());
    TEST_ASSERT_EQUAL_STRING("[REDACTED]",
                             redact_secret("token-12345").c_str());
}

static void test_mask_secret(void)
{
    TEST_ASSERT_EQUAL_STRING("", mask_secret("").c_str());
    TEST_ASSERT_EQUAL_STRING("****", mask_secret("abcd").c_str()); // size <= keep
    TEST_ASSERT_EQUAL_STRING("****5678", mask_secret("12345678").c_str());
    TEST_ASSERT_EQUAL_STRING("*****678", mask_secret("12345678", '*', 3).c_str());
    TEST_ASSERT_EQUAL_STRING("----5678", mask_secret("12345678", '-', 4).c_str());
}

static void test_take_snapshot(void)
{
    Snapshot snap = take_snapshot(123456, 1024 * 1024, 512 * 1024,
                                  "poweron", false);
    TEST_ASSERT_EQUAL_UINT32(123456, snap.uptime_ms);
    TEST_ASSERT_EQUAL_UINT32(1024u * 1024u, snap.free_heap_bytes);
    TEST_ASSERT_EQUAL_UINT32(512u * 1024u, snap.min_free_heap_bytes);
    TEST_ASSERT_EQUAL_STRING("poweron", snap.reset_reason);
    TEST_ASSERT_FALSE(snap.any_service_failed);

    // Null reset reason defaults to "unknown".
    Snapshot snap2 = take_snapshot(0, 0, 0, nullptr, true);
    TEST_ASSERT_EQUAL_STRING("unknown", snap2.reset_reason);
    TEST_ASSERT_TRUE(snap2.any_service_failed);
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_category_names);
    RUN_TEST(test_redact_secret);
    RUN_TEST(test_mask_secret);
    RUN_TEST(test_take_snapshot);
    return UNITY_END();
}
