// Host tests for PCA9685 batching (gate item 6): N light writes → ≤ expected
// I2C transactions; redundant writes skipped.
#include <cstdint>
#include <vector>

#include <unity.h>

#include "lumen/hardware/drivers/pca9685_driver.h"

#include "mock_i2c_bus.h"

using lumen::MockI2cBus;
using lumen::Pca9685Driver;

namespace
{

extern "C" void setUp(void) {}
extern "C" void tearDown(void) {}

} // namespace

// ---------------------------------------------------------------------------
static void test_init_writes_in_batches(void)
{
    MockI2cBus bus;
    bus.add_device(0x40, {0x00});

    Pca9685Driver pca(bus, 0x40, 1000);
    TEST_ASSERT_EQUAL(ESP_OK, pca.initialize());

    // init: mode1 + mode2 + prescale(3 writes: sleep, prescale, restart) +
    // all-channels-off flush (16 contiguous channels batched into 1 txn).
    // Total ≤ 6 transactions.
    TEST_ASSERT_TRUE(bus.transaction_count() <= 6);
    TEST_ASSERT_EQUAL(0, pca.pending_count());
}

static void test_flush_batches_contiguous_channels(void)
{
    MockI2cBus bus;
    bus.add_device(0x40, {0x00});
    Pca9685Driver pca(bus, 0x40, 1000);
    TEST_ASSERT_EQUAL(ESP_OK, pca.initialize());
    bus.clear_transactions();

    // Non-contiguous channels: 0, 1 (contiguous run) and 5 (separate).
    TEST_ASSERT_EQUAL(ESP_OK, pca.queue_update(0, 100, true));
    TEST_ASSERT_EQUAL(ESP_OK, pca.queue_update(1, 200, true));
    TEST_ASSERT_EQUAL(ESP_OK, pca.queue_update(5, 50, true));
    TEST_ASSERT_EQUAL(3, pca.pending_count());

    TEST_ASSERT_EQUAL(ESP_OK, pca.flush());
    TEST_ASSERT_EQUAL(0, pca.pending_count());

    // 2 transactions: one for channels 0-1, one for channel 5.
    TEST_ASSERT_EQUAL(2, bus.transaction_count());
}

static void test_redundant_flush_skipped(void)
{
    MockI2cBus bus;
    bus.add_device(0x40, {0x00});
    Pca9685Driver pca(bus, 0x40, 1000);
    TEST_ASSERT_EQUAL(ESP_OK, pca.initialize());
    bus.clear_transactions();

    TEST_ASSERT_EQUAL(ESP_OK, pca.queue_update(0, 100, true));
    TEST_ASSERT_EQUAL(ESP_OK, pca.flush()); // writes
    TEST_ASSERT_EQUAL(1, bus.transaction_count());

    bus.clear_transactions();
    // flush again with nothing pending → no I2C transactions.
    TEST_ASSERT_EQUAL(ESP_OK, pca.flush());
    TEST_ASSERT_EQUAL(0, bus.transaction_count());
    TEST_ASSERT_EQUAL(0, pca.pending_count());
}

static void test_queue_update_validation(void)
{
    MockI2cBus bus;
    bus.add_device(0x40, {0x00});
    Pca9685Driver pca(bus, 0x40, 1000);
    TEST_ASSERT_EQUAL(ESP_OK, pca.initialize());

    // Channel out of range.
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, pca.queue_update(16, 100, true));
    // Duty out of range (12-bit max 4095).
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, pca.queue_update(0, 4096, true));
}

static void test_set_frequency_validation(void)
{
    MockI2cBus bus;
    bus.add_device(0x40, {0x00});
    Pca9685Driver pca(bus, 0x40, 1000);
    TEST_ASSERT_EQUAL(ESP_OK, pca.initialize());

    TEST_ASSERT_EQUAL(ESP_OK, pca.set_frequency(1000));
    // PCA9685 valid range ~24-1526 Hz.
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, pca.set_frequency(10));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, pca.set_frequency(5000));
}

// ---------------------------------------------------------------------------
// Reliability: I2C peripheral failure (PCA9685 unplugged / bus fault) and
// recovery. A mock bus with error injection stands in for the physical fault.

static void ready(MockI2cBus &bus, Pca9685Driver &pca)
{
    bus.add_device(0x40, {0x20}); // MODE1 after init: auto-increment, awake
    TEST_ASSERT_EQUAL(ESP_OK, pca.initialize());
    bus.clear_transactions();
}

static void test_failure_backs_off_without_bus_traffic(void)
{
    MockI2cBus bus;
    Pca9685Driver pca(bus, 0x40, 1000);
    ready(bus, pca);
    TEST_ASSERT_TRUE(pca.online());

    bus.inject_error(0x40, ESP_ERR_TIMEOUT); // chip gone
    pca.queue_update(3, 2000, true);
    TEST_ASSERT_NOT_EQUAL(ESP_OK, pca.flush());
    TEST_ASSERT_FALSE(pca.online());
    TEST_ASSERT_EQUAL_UINT32(1, pca.error_count());

    // While lost, flushes do not touch the bus (no I2C timeouts per tick).
    bus.clear_transactions();
    for (uint32_t i = 0; i < Pca9685Driver::kRecoveryInterval; ++i) {
        TEST_ASSERT_EQUAL(ESP_FAIL, pca.flush());
    }
    TEST_ASSERT_EQUAL(0, bus.transaction_count());

    // Then exactly one recovery attempt; still failing -> back off again.
    TEST_ASSERT_EQUAL(ESP_FAIL, pca.flush());
    TEST_ASSERT_TRUE(bus.transaction_count() >= 1 && bus.transaction_count() <= 2);
    TEST_ASSERT_EQUAL_UINT32(2, pca.error_count());
    TEST_ASSERT_FALSE(pca.online());
}

static void test_recovery_reconfigures_and_rewrites_every_channel(void)
{
    MockI2cBus bus;
    Pca9685Driver pca(bus, 0x40, 1000);
    ready(bus, pca);
    pca.queue_update(0, 1000, true);
    TEST_ASSERT_EQUAL(ESP_OK, pca.flush());

    bus.inject_error(0x40, ESP_ERR_TIMEOUT);
    pca.queue_update(7, 3000, true); // state changes while the chip is gone
    pca.flush();
    for (uint32_t i = 0; i < Pca9685Driver::kRecoveryInterval; ++i) pca.flush();

    bus.inject_error(0x40, ESP_OK); // chip back
    bus.clear_transactions();
    TEST_ASSERT_EQUAL(ESP_OK, pca.flush());
    TEST_ASSERT_TRUE(pca.online());
    TEST_ASSERT_EQUAL_UINT32(1, pca.recovery_count());
    TEST_ASSERT_EQUAL(0, pca.pending_count());

    // Reconfigure: MODE2, sleep, prescale, wake, restart, then one batched
    // write of all 16 channels carrying the latest state (ch 0 and ch 7).
    const auto &t = bus.transactions();
    TEST_ASSERT_EQUAL(6, t.size());
    TEST_ASSERT_EQUAL_UINT8(0x01, t[0].data[0]); // MODE2
    TEST_ASSERT_EQUAL_UINT8(0xFE, t[2].data[0]); // PRE_SCALE
    const auto &all = t.back().data;
    TEST_ASSERT_EQUAL(1 + 16 * 4, all.size());
    TEST_ASSERT_EQUAL_UINT8(0x06, all[0]); // LED0_ON_L
    const auto off = [&](int ch) { return all[1 + ch * 4 + 2] | ((all[1 + ch * 4 + 3] & 0x1F) << 8); };
    TEST_ASSERT_EQUAL(1000, off(0));
    TEST_ASSERT_EQUAL(3000, off(7));
    TEST_ASSERT_EQUAL(0x1000, off(1)); // untouched channels stay fully off
}

static void test_power_cycled_chip_is_woken_and_rewritten(void)
{
    MockI2cBus bus;
    Pca9685Driver pca(bus, 0x40, 1000);
    ready(bus, pca);
    pca.queue_update(2, 1500, true);
    TEST_ASSERT_EQUAL(ESP_OK, pca.flush());

    // The chip lost power and came back: writes succeed, but it sleeps with
    // every register reset. The periodic MODE1 check notices it within
    // kSleepCheckInterval flushes and rewrites all channels.
    bus.set_device_byte(0x40, 0x10);
    uint32_t n = 0;
    while (pca.recovery_count() == 0 && n < Pca9685Driver::kSleepCheckInterval) {
        bus.clear_transactions();
        TEST_ASSERT_EQUAL(ESP_OK, pca.flush());
        ++n;
    }
    TEST_ASSERT_EQUAL_UINT32(1, pca.recovery_count());
    TEST_ASSERT_EQUAL(0, pca.pending_count());
    TEST_ASSERT_EQUAL(1 + 16 * 4, bus.transactions().back().data.size());
    TEST_ASSERT_TRUE(pca.online());
    bus.set_device_byte(0x40, 0x20); // awake again: no further recoveries
    for (uint32_t i = 0; i < 2 * Pca9685Driver::kSleepCheckInterval; ++i) pca.flush();
    TEST_ASSERT_EQUAL_UINT32(1, pca.recovery_count());
}

static void test_mode1_read_failure_marks_lost(void)
{
    MockI2cBus bus;
    Pca9685Driver pca(bus, 0x40, 1000);
    ready(bus, pca);
    bus.inject_error(0x40, ESP_ERR_TIMEOUT);
    // Nothing to write, but the idle chip's disappearance is still detected
    // by the periodic MODE1 check.
    uint32_t n = 0;
    while (pca.online() && n < Pca9685Driver::kSleepCheckInterval) {
        pca.flush();
        ++n;
    }
    TEST_ASSERT_FALSE(pca.online());
    TEST_ASSERT_EQUAL_UINT32(1, pca.error_count());
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_init_writes_in_batches);
    RUN_TEST(test_flush_batches_contiguous_channels);
    RUN_TEST(test_redundant_flush_skipped);
    RUN_TEST(test_queue_update_validation);
    RUN_TEST(test_set_frequency_validation);
    RUN_TEST(test_failure_backs_off_without_bus_traffic);
    RUN_TEST(test_recovery_reconfigures_and_rewrites_every_channel);
    RUN_TEST(test_power_cycled_chip_is_woken_and_rewritten);
    RUN_TEST(test_mode1_read_failure_marks_lost);
    return UNITY_END();
}
