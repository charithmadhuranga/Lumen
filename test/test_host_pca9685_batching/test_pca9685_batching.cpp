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
    return UNITY_END();
}
