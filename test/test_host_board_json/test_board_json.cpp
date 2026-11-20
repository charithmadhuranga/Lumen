// Host tests for Phase 7 board-profile JSON + validation core.
// Exercises board_profile_to_json / load round-trip and validate_profile_json
// (PinValidator) without hardware.
#include <cstdio>
#include <string>

#include <unity.h>

#include "lumen/hardware/board_profile.h"
#include "lumen/hardware/board_profile_loader.h"
#include "lumen/web/board_json.hpp"

using lumen::BoardProfile;
using lumen::BoardPeripheral;
using lumen::BoardResource;
using lumen::ValidationReason;
using lumen::ValidationReport;
using lumen::web::board_profile_to_json;
using lumen::web::validate_profile_json;
using lumen::web::validation_report_to_json;
using lumen::web::validation_reason_code;

static const char *kValidProfile =
    R"({"schema_version":1,"id":"test_board","name":"Test Board","mcu":"esp32",)"
    R"("resources":[{"id":"gpio13","type":"gpio","gpio":13,"direction":"output","safe_state":false}],)"
    R"("peripherals":[]})";

static const char *kDuplicateGpio =
    R"({"schema_version":1,"id":"dup","name":"Dup","mcu":"esp32",)"
    R"("resources":[{"id":"a","type":"gpio","gpio":13,"direction":"output"},)"
    R"({"id":"b","type":"gpio","gpio":13,"direction":"output"}],"peripherals":[]})";

static const char *kInputOnlyAsOutput =
    R"({"schema_version":1,"id":"io","name":"IO","mcu":"esp32",)"
    R"("resources":[{"id":"a","type":"gpio","gpio":34,"direction":"output"}],"peripherals":[]})";

static const char *kUnknownMcu =
    R"({"schema_version":1,"id":"x","name":"X","mcu":"rp2040","resources":[],"peripherals":[]})";

static const char *kBadJson = "{ this is not valid json ";

static bool report_has(const ValidationReport &r, ValidationReason reason)
{
    for (const auto &e : r.errors) {
        if (e.reason == reason) return true;
    }
    for (const auto &e : r.warnings) {
        if (e.reason == reason) return true;
    }
    return false;
}

static void test_reason_codes_stable(void)
{
    TEST_ASSERT_EQUAL_STRING("kDuplicateAssignment",
                             validation_reason_code(ValidationReason::kDuplicateAssignment));
    TEST_ASSERT_EQUAL_STRING("kInputOnlyAsOutput",
                             validation_reason_code(ValidationReason::kInputOnlyAsOutput));
    TEST_ASSERT_EQUAL_STRING("kProfileUnknownMcu",
                             validation_reason_code(ValidationReason::kProfileUnknownMcu));
}

static void test_valid_profile_no_errors(void)
{
    bool parse_ok = false;
    auto report = validate_profile_json(kValidProfile, parse_ok);
    TEST_ASSERT_TRUE(parse_ok);
    TEST_ASSERT_FALSE(report.has_errors());
}

static void test_duplicate_gpio_rejected(void)
{
    bool parse_ok = false;
    auto report = validate_profile_json(kDuplicateGpio, parse_ok);
    TEST_ASSERT_TRUE(parse_ok);
    TEST_ASSERT_TRUE(report.has_errors());
    TEST_ASSERT_TRUE(report_has(report, ValidationReason::kDuplicateAssignment));
}

static void test_input_only_as_output_rejected(void)
{
    bool parse_ok = false;
    auto report = validate_profile_json(kInputOnlyAsOutput, parse_ok);
    TEST_ASSERT_TRUE(parse_ok);
    TEST_ASSERT_TRUE(report.has_errors());
    TEST_ASSERT_TRUE(report_has(report, ValidationReason::kInputOnlyAsOutput));
}

static void test_unknown_mcu_rejected(void)
{
    bool parse_ok = false;
    auto report = validate_profile_json(kUnknownMcu, parse_ok);
    TEST_ASSERT_TRUE(parse_ok); // JSON parsed, but MCU unknown
    TEST_ASSERT_TRUE(report.has_errors());
    TEST_ASSERT_TRUE(report_has(report, ValidationReason::kProfileUnknownMcu));
}

static void test_bad_json_reports_invalid(void)
{
    bool parse_ok = true;
    auto report = validate_profile_json(kBadJson, parse_ok);
    TEST_ASSERT_FALSE(parse_ok);
    TEST_ASSERT_TRUE(report.has_errors());
    TEST_ASSERT_TRUE(report_has(report, ValidationReason::kProfileInvalidJson));
}

static void test_report_json_contains_codes(void)
{
    bool parse_ok = false;
    auto report = validate_profile_json(kDuplicateGpio, parse_ok);
    std::string json = validation_report_to_json(report);
    TEST_ASSERT_TRUE(json.find("\"code\":\"kDuplicateAssignment\"") != std::string::npos);
    TEST_ASSERT_TRUE(json.find("\"has_errors\":true") != std::string::npos);
    TEST_ASSERT_TRUE(json.find("\"errors\"") != std::string::npos);
}

static void test_profile_serialize_round_trip(void)
{
    BoardProfile profile;
    profile.schema_version = 1;
    profile.id = "rt";
    profile.name = "Round Trip";
    profile.manufacturer = "Acme";
    profile.mcu = "esp32";
    profile.description = "round-trip test";

    BoardResource r;
    r.id = "gpio13";
    r.type = "gpio";
    r.gpio = 13;
    r.direction = "output";
    r.safe_state_set = true;
    r.safe_state = false;
    profile.resources.push_back(r);

    BoardPeripheral p;
    p.id = "relay1";
    p.type = "relay";
    p.bus = "gpio13";
    p.channels = 1;
    profile.peripherals.push_back(p);

    std::string json = board_profile_to_json(profile);
    TEST_ASSERT_TRUE(json.find("\"id\":\"rt\"") != std::string::npos);
    TEST_ASSERT_TRUE(json.find("\"gpio\":13") != std::string::npos);
    TEST_ASSERT_TRUE(json.find("\"relay1\"") != std::string::npos);

    BoardProfile parsed;
    TEST_ASSERT_TRUE(lumen::load_board_profile(json, parsed));
    TEST_ASSERT_EQUAL_STRING("rt", parsed.id.c_str());
    TEST_ASSERT_EQUAL_STRING("esp32", parsed.mcu.c_str());
    TEST_ASSERT_EQUAL_STRING("Round Trip", parsed.name.c_str());
    TEST_ASSERT_EQUAL(1u, parsed.resources.size());
    TEST_ASSERT_EQUAL(13, parsed.resources[0].gpio);
    TEST_ASSERT_EQUAL_STRING("output", parsed.resources[0].direction.c_str());
    TEST_ASSERT_EQUAL(1u, parsed.peripherals.size());
    TEST_ASSERT_EQUAL_STRING("relay1", parsed.peripherals[0].id.c_str());
}

int main(int /*argc*/, char ** /*argv*/)
{
    UNITY_BEGIN();
    RUN_TEST(test_reason_codes_stable);
    RUN_TEST(test_valid_profile_no_errors);
    RUN_TEST(test_duplicate_gpio_rejected);
    RUN_TEST(test_input_only_as_output_rejected);
    RUN_TEST(test_unknown_mcu_rejected);
    RUN_TEST(test_bad_json_reports_invalid);
    RUN_TEST(test_report_json_contains_codes);
    RUN_TEST(test_profile_serialize_round_trip);
    return UNITY_END();
}