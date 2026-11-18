// Host smoke test — proves the native test harness runs and that the
// shared firmware version header is what the device firmware uses
// (AGENTS.md §11: every feature ships with tests).
#include <cctype>
#include <cstring>

#include <unity.h>

#include "lumen/version.hpp"

extern "C" void setUp(void) {}
extern "C" void tearDown(void) {}

// The build injects LUMEN_FIRMWARE_VERSION through platformio.ini, so the
// host test asserts on the exact string the device firmware prints.
static void test_version_is_nonempty(void)
{
    TEST_ASSERT_NOT_EQUAL(0, std::strlen(lumen::kVersion));
}

static void test_version_is_semver_like(void)
{
    // Expect MAJOR.MINOR.PATCH with an optional -suffix (e.g. 0.1.0-dev).
    const char *v = lumen::kVersion;
    int digits = 0;
    int dots = 0;
    for (const char *p = v; *p != '\0'; ++p) {
        if (std::isdigit(static_cast<unsigned char>(*p))) {
            ++digits;
        } else if (*p == '.') {
            ++dots;
        } else if (*p == '-') {
            break; // pre-release suffix starts
        } else {
            TEST_FAIL_MESSAGE("unexpected character in version string");
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(digits >= 3, "expected MAJOR.MINOR.PATCH digits");
    TEST_ASSERT_EQUAL_INT_MESSAGE(2, dots, "expected two version dots");
}

static void test_version_differs_from_unset_sentinel(void)
{
    TEST_ASSERT_NOT_EQUAL(0, std::strcmp(lumen::kVersion, "0.0.0"));
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_version_is_nonempty);
    RUN_TEST(test_version_is_semver_like);
    RUN_TEST(test_version_differs_from_unset_sentinel);
    return UNITY_END();
}
