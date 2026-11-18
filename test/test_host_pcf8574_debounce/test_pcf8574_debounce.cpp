// Host tests for PCF8574 debounce + change detection (gate item 7).
#include <cstdint>
#include <vector>

#include <unity.h>

#include "lumen/hardware/drivers/pcf8574_driver.h"

#include "mock_i2c_bus.h"

using lumen::InputChangeEvent;
using lumen::MockI2cBus;
using lumen::Pcf8574Driver;

namespace
{

extern "C" void setUp(void) {}
extern "C" void tearDown(void) {}

} // namespace

// ---------------------------------------------------------------------------
static void test_change_detected_after_debounce(void)
{
    MockI2cBus bus;
    // active_low=true: raw 0xFF → all levels false; raw 0xFE → pin0 level true.
    bus.add_device(0x20, {0xFF});
    Pcf8574Driver pcf(bus, 0x20, 0, /* active_low */ true, /* debounce_ms */ 20);

    InputChangeEvent events[8];
    size_t count = 0;

    // Initial read at t=0.
    TEST_ASSERT_EQUAL(ESP_OK, pcf.poll(0, events, 8, count));
    TEST_ASSERT_EQUAL(0, count);

    // Change pin 0 at t=5 (debounce not yet elapsed).
    bus.set_device_byte(0x20, 0xFE);
    TEST_ASSERT_EQUAL(ESP_OK, pcf.poll(5, events, 8, count));
    TEST_ASSERT_EQUAL(0, count); // debounce pending

    // After debounce (t=30, >20ms since change at t=5).
    TEST_ASSERT_EQUAL(ESP_OK, pcf.poll(30, events, 8, count));
    TEST_ASSERT_EQUAL(1, count);
    TEST_ASSERT_EQUAL(0, events[0].device_index);
    TEST_ASSERT_EQUAL(0, events[0].pin);
    TEST_ASSERT_EQUAL(true, events[0].level);
    TEST_ASSERT_EQUAL(30, events[0].timestamp_ms);
}

static void test_no_event_when_stable(void)
{
    MockI2cBus bus;
    bus.add_device(0x20, {0xFF});
    Pcf8574Driver pcf(bus, 0x20, 0, true, 20);

    InputChangeEvent events[8];
    size_t count = 0;

    TEST_ASSERT_EQUAL(ESP_OK, pcf.poll(0, events, 8, count));
    TEST_ASSERT_EQUAL(0, count);

    // Same byte again → no change.
    TEST_ASSERT_EQUAL(ESP_OK, pcf.poll(100, events, 8, count));
    TEST_ASSERT_EQUAL(0, count);
}

static void test_active_low_polarity(void)
{
    MockI2cBus bus;
    bus.add_device(0x20, {0xFE}); // pin0 raw low
    Pcf8574Driver pcf(bus, 0x20, 0, /* active_low */ true, 0);

    InputChangeEvent events[8];
    size_t count = 0;
    TEST_ASSERT_EQUAL(ESP_OK, pcf.poll(0, events, 8, count));
    TEST_ASSERT_EQUAL(1, count);
    TEST_ASSERT_EQUAL(true, events[0].level); // active-low: raw low → level true

    // Now active-high interpretation: same raw 0xFE → pins 1-7 go high
    // (active-low=false does not invert), pin0 stays low (level false, no
    // change from initial false state).
    bus.set_device_byte(0x20, 0xFE);
    Pcf8574Driver pcf2(bus, 0x20, 1, /* active_low */ false, 0);
    size_t count2 = 0;
    TEST_ASSERT_EQUAL(ESP_OK, pcf2.poll(0, events, 8, count2));
    TEST_ASSERT_EQUAL(7, count2); // pins 1-7 all transition false→true
    TEST_ASSERT_EQUAL(true, events[0].level); // first changed pin is high
}

static void test_multiple_pins_change(void)
{
    MockI2cBus bus;
    bus.add_device(0x20, {0xFF});
    Pcf8574Driver pcf(bus, 0x20, 0, true, 0);

    // Change pin 7 (0xFF → 0x7F: bit7 changes 1→0).
    bus.set_device_byte(0x20, 0x7F);
    InputChangeEvent events[8];
    size_t count = 0;
    TEST_ASSERT_EQUAL(ESP_OK, pcf.poll(10, events, 8, count));
    TEST_ASSERT_EQUAL(1, count);
    TEST_ASSERT_EQUAL(7, events[0].pin);
    TEST_ASSERT_EQUAL(true, events[0].level);

    // pin0 changes too (0x7F → 0x7E: bit0 changes 1→0, pin7 stays 0).
    bus.set_device_byte(0x20, 0x7E); // pin0 low, pin7 still low
    count = 0;
    TEST_ASSERT_EQUAL(ESP_OK, pcf.poll(20, events, 8, count));
    TEST_ASSERT_EQUAL(1, count); // only pin0
    TEST_ASSERT_EQUAL(0, events[0].pin);
    TEST_ASSERT_EQUAL(true, events[0].level);
}

static void test_health_on_read_failure(void)
{
    MockI2cBus bus;
    bus.add_device(0x20, {0xFF});
    bus.inject_error(0x20, ESP_ERR_TIMEOUT);
    Pcf8574Driver pcf(bus, 0x20, 0, true, 0);

    InputChangeEvent events[8];
    size_t count = 0;
    TEST_ASSERT_EQUAL(ESP_ERR_TIMEOUT, pcf.poll(0, events, 8, count));
    TEST_ASSERT_EQUAL(1, pcf.read_failure_count());
    TEST_ASSERT_FALSE(pcf.healthy());
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_change_detected_after_debounce);
    RUN_TEST(test_no_event_when_stable);
    RUN_TEST(test_active_low_polarity);
    RUN_TEST(test_multiple_pins_change);
    RUN_TEST(test_health_on_read_failure);
    return UNITY_END();
}
