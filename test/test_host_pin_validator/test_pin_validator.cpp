// Host tests for the PinValidator (SPEC §5.3). Covers duplicate GPIO,
// input-only-as-output, strapping misuse, flash-reserved pins, board-reserved
// pins, unresolved peripheral references, bus conflicts, and PWM/RMT limits.
#include <cstring>
#include <string>
#include <vector>

#include <unity.h>

#include "lumen/hardware/board_profile.h"
#include "lumen/hardware/board_profile_loader.h"
#include "lumen/hardware/mcu_capabilities.h"
#include "lumen/hardware/pin_validator.h"
#include "lumen/hardware/validation_report.h"

using lumen::BoardPeripheral;
using lumen::BoardProfile;
using lumen::BoardProfileIndex;
using lumen::BoardResource;
using lumen::McuCapabilities;
using lumen::McuCapabilityRegistry;
using lumen::PinValidator;
using lumen::ValidationEntry;
using lumen::ValidationReason;
using lumen::ValidationReport;

namespace
{

// Build a minimal valid ESP32 profile: one GPIO output + one I2C resource.
BoardProfile make_valid_profile()
{
    BoardProfile p;
    p.schema_version = 1;
    p.id = "test_board";
    p.name = "Test Board";
    p.mcu = "esp32";

    BoardResource led;
    led.id = "led0";
    led.type = "gpio";
    led.gpio = 2;
    led.direction = "output";
    p.resources.push_back(led);

    BoardResource i2c;
    i2c.id = "i2c0";
    i2c.type = "i2c";
    p.resources.push_back(i2c);

    BoardPeripheral pca;
    pca.id = "pca9685_0";
    pca.type = "pca9685";
    pca.bus = "i2c0";
    pca.address = 0x40;
    pca.channels = 16;
    p.peripherals.push_back(pca);

    return p;
}

BoardProfileIndex index_of(const BoardProfile &p)
{
    BoardProfileIndex idx;
    idx.index(p);
    return idx;
}

void expect_no_errors(const ValidationReport &report)
{
    TEST_ASSERT_EQUAL_size_t(0, report.error_count());
}

void expect_error_count(const ValidationReport &report,
                        std::size_t expected)
{
    TEST_ASSERT_EQUAL_size_t(expected, report.error_count());
}

bool has_reason(const ValidationReport &report, ValidationReason reason)
{
    for (const auto &e : report.errors) {
        if (e.reason == reason) {
            return true;
        }
    }
    return false;
}

bool has_warning(const ValidationReport &report, ValidationReason reason)
{
    for (const auto &e : report.warnings) {
        if (e.reason == reason) {
            return true;
        }
    }
    return false;
}

} // namespace

extern "C" void setUp(void) {}
extern "C" void tearDown(void) {}

// ---------------------------------------------------------------------------
static void test_valid_profile_passes(void)
{
    BoardProfile p = make_valid_profile();
    const McuCapabilities *cap = McuCapabilityRegistry::find("esp32");
    TEST_ASSERT_TRUE(cap != nullptr);
    PinValidator v(*cap, {});
    ValidationReport report = v.validate(index_of(p));
    expect_no_errors(report);
}

static void test_unknown_mcu_rejected(void)
{
    BoardProfile p = make_valid_profile();
    p.mcu = "esp32xx"; // unknown
    const McuCapabilities *cap = McuCapabilityRegistry::find("esp32");
    TEST_ASSERT_TRUE(cap != nullptr);
    PinValidator v(*cap, {});
    ValidationReport report = v.validate(index_of(p));
    TEST_ASSERT_EQUAL_size_t(1, report.error_count());
    TEST_ASSERT_TRUE(has_reason(report, ValidationReason::kProfileUnknownMcu));
}

static void test_duplicate_gpio_detected(void)
{
    BoardProfile p = make_valid_profile();
    BoardResource dup;
    dup.id = "led1";
    dup.type = "gpio";
    dup.gpio = 2; // same as led0
    dup.direction = "output";
    p.resources.push_back(dup);

    const McuCapabilities *cap = McuCapabilityRegistry::find("esp32");
    PinValidator v(*cap, {});
    ValidationReport report = v.validate(index_of(p));
    TEST_ASSERT_TRUE(has_reason(report, ValidationReason::kDuplicateAssignment));
}

static void test_input_only_as_output_detected(void)
{
    BoardProfile p = make_valid_profile();
    BoardResource bad;
    bad.id = "adc0";
    bad.type = "gpio";
    bad.gpio = 34; // input-only on ESP32
    bad.direction = "output";
    p.resources.push_back(bad);

    const McuCapabilities *cap = McuCapabilityRegistry::find("esp32");
    PinValidator v(*cap, {});
    ValidationReport report = v.validate(index_of(p));
    TEST_ASSERT_TRUE(
        has_reason(report, ValidationReason::kInputOnlyAsOutput));
}

static void test_strapping_pin_warning(void)
{
    BoardProfile p = make_valid_profile();
    BoardResource btn;
    btn.id = "btn0";
    btn.type = "gpio";
    btn.gpio = 0; // strapping on ESP32
    btn.direction = "input";
    p.resources.push_back(btn);

    const McuCapabilities *cap = McuCapabilityRegistry::find("esp32");
    PinValidator v(*cap, {});
    ValidationReport report = v.validate(index_of(p));
    TEST_ASSERT_TRUE(
        has_warning(report, ValidationReason::kStrappingPinMisused));
    // Not an error.
    TEST_ASSERT_FALSE(has_reason(report, ValidationReason::kStrappingPinMisused));
}

static void test_flash_reserved_pin_rejected(void)
{
    BoardProfile p = make_valid_profile();
    BoardResource bad;
    bad.id = "flash0";
    bad.type = "gpio";
    bad.gpio = 6; // flash reserved on ESP32
    bad.direction = "output";
    p.resources.push_back(bad);

    const McuCapabilities *cap = McuCapabilityRegistry::find("esp32");
    PinValidator v(*cap, {});
    ValidationReport report = v.validate(index_of(p));
    TEST_ASSERT_TRUE(
        has_reason(report, ValidationReason::kFlashReservedPin));
}

static void test_board_reserved_pin_rejected(void)
{
    BoardProfile p = make_valid_profile();
    const McuCapabilities *cap = McuCapabilityRegistry::find("esp32");
    PinValidator v(*cap, {2}); // GPIO2 board-reserved
    // led0 uses GPIO2 → board reserved error
    ValidationReport report = v.validate(index_of(p));
    TEST_ASSERT_TRUE(
        has_reason(report, ValidationReason::kBoardReservedPin));
}

static void test_unresolved_peripheral_reference_rejected(void)
{
    BoardProfile p = make_valid_profile();
    BoardPeripheral bad;
    bad.id = "ghost";
    bad.type = "pcf8574";
    bad.bus = "i2c_nonexistent"; // not declared
    bad.address = 0x20;
    p.peripherals.push_back(bad);

    const McuCapabilities *cap = McuCapabilityRegistry::find("esp32");
    PinValidator v(*cap, {});
    ValidationReport report = v.validate(index_of(p));
    TEST_ASSERT_TRUE(
        has_reason(report, ValidationReason::kPeripheralUnresolved));
}

static void test_pwm_overflow_detected(void)
{
    BoardProfile p = make_valid_profile();
    // ESP32 LEDC provides 16 channels; request 17.
    for (int i = 0; i < 17; ++i) {
        BoardResource pwm;
        pwm.id = "pwm" + std::to_string(i);
        pwm.type = "pwm";
        pwm.gpio = 5 + i; // distinct GPIOs
        pwm.direction = "output";
        p.resources.push_back(pwm);
    }

    const McuCapabilities *cap = McuCapabilityRegistry::find("esp32");
    PinValidator v(*cap, {});
    ValidationReport report = v.validate(index_of(p));
    TEST_ASSERT_TRUE(
        has_reason(report, ValidationReason::kPwmChannelOverflow));
}

static void test_gpio_does_not_exist_rejected(void)
{
    BoardProfile p = make_valid_profile();
    BoardResource bad;
    bad.id = "gpio99";
    bad.type = "gpio";
    bad.gpio = 99; // ESP32 only has 34 GPIOs
    bad.direction = "output";
    p.resources.push_back(bad);

    const McuCapabilities *cap = McuCapabilityRegistry::find("esp32");
    PinValidator v(*cap, {});
    ValidationReport report = v.validate(index_of(p));
    TEST_ASSERT_TRUE(
        has_reason(report, ValidationReason::kGpioDoesNotExist));
}

static void test_i2c_address_conflict_detected(void)
{
    BoardProfile p = make_valid_profile();
    BoardPeripheral dup;
    dup.id = "pca9685_1";
    dup.type = "pca9685";
    dup.bus = "i2c0"; // same bus
    dup.address = 0x40; // same address as pca9685_0
    p.peripherals.push_back(dup);

    const McuCapabilities *cap = McuCapabilityRegistry::find("esp32");
    PinValidator v(*cap, {});
    ValidationReport report = v.validate(index_of(p));
    TEST_ASSERT_TRUE(has_reason(report, ValidationReason::kI2cConflict));
}

namespace
{

// Load a built-in board profile from `boards/` and run the PinValidator.
// Returns the report.
ValidationReport load_and_validate(const char *filename)
{
    std::string path = std::string(LUMEN_REPO_ROOT) + "/boards/" + filename;
    BoardProfile profile;
    TEST_ASSERT_TRUE_MESSAGE(load_board_profile_file(path, profile),
                             "board profile file should load");

    BoardProfileIndex idx;
    idx.index(profile);

    const McuCapabilities *cap = McuCapabilityRegistry::find(profile.mcu);
    TEST_ASSERT_TRUE_MESSAGE(cap != nullptr, "MCU capability table should exist");

    PinValidator v(*cap, {});
    return v.validate(idx);
}

} // namespace

// ---------------------------------------------------------------------------
static void test_builtin_profiles_load_and_validate(void)
{
    // Every built-in profile must load against the schema and pass semantic
    // pin validation with no hard errors (warnings are acceptable for
    // unverified addresses / strapping pins).
    const char *profiles[] = {
        "kincony_kc868_ap.json",
        "generic_esp32.json",
        "generic_esp32s3.json",
        "generic_esp32_pca9685.json",
        "generic_esp32_pcf8574.json",
        "generic_esp32_pwm.json",
    };

    for (const char *name : profiles) {
        ValidationReport report = load_and_validate(name);
        TEST_ASSERT_EQUAL_size_t_MESSAGE(
            0, report.error_count(),
            ("profile " + std::string(name) + " has validation errors").c_str());
    }
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_valid_profile_passes);
    RUN_TEST(test_unknown_mcu_rejected);
    RUN_TEST(test_duplicate_gpio_detected);
    RUN_TEST(test_input_only_as_output_detected);
    RUN_TEST(test_strapping_pin_warning);
    RUN_TEST(test_flash_reserved_pin_rejected);
    RUN_TEST(test_board_reserved_pin_rejected);
    RUN_TEST(test_unresolved_peripheral_reference_rejected);
    RUN_TEST(test_pwm_overflow_detected);
    RUN_TEST(test_gpio_does_not_exist_rejected);
    RUN_TEST(test_i2c_address_conflict_detected);
    RUN_TEST(test_builtin_profiles_load_and_validate);
    return UNITY_END();
}
