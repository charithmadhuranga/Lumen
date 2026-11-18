// Event bus host tests — ordering, bounds, drop policy, coalescing,
// eviction and subscription limits (phase-01 gate item 6).
#include <cstddef>
#include <cstdint>
#include <vector>

#include <unity.h>

#include "lumen/core/event_bus.hpp"

using lumen::Event;
using lumen::EventBus;
using lumen::EventBusStats;
using lumen::EventId;
using lumen::EventPayload;
using lumen::EventPriority;
using lumen::IClock;
using lumen::ILock;
using lumen::SubscriptionId;
using lumen::SubscribeOptions;

namespace
{

struct NoopLock final : ILock {
    void lock() override {}
    void unlock() override {}
};

struct FakeClock final : IClock {
    uint32_t now = 0;
    uint32_t ms() override
    {
        return now;
    }
};

struct Recorder {
    std::vector<Event> got;
};

void record(const Event &event, void *user)
{
    static_cast<Recorder *>(user)->got.push_back(event);
}

EventPayload payload_with_byte(uint8_t value)
{
    EventPayload p{};
    p.data.bytes[0] = value;
    return p;
}

// Shared fixtures re-created per test.
NoopLock g_lock;
FakeClock g_clock;
Event g_pool[64];

void setUp()
{
    g_clock.now = 0;
}

void tearDown() {}

} // namespace

// ---------------------------------------------------------------------------
static void test_subscribe_rejects_invalid_options(void)
{
    EventBus bus(g_lock, g_clock, g_pool, 64);
    Recorder rec;

    SubscribeOptions no_handler;
    no_handler.capacity = 4;
    TEST_ASSERT_EQUAL_INT(lumen::kInvalidSubscription, bus.subscribe(no_handler));

    SubscribeOptions zero_cap;
    zero_cap.handler = record;
    zero_cap.user = &rec;
    zero_cap.capacity = 0;
    TEST_ASSERT_EQUAL_INT(lumen::kInvalidSubscription, bus.subscribe(zero_cap));
}

static void test_fifo_ordering_and_sequence(void)
{
    EventBus bus(g_lock, g_clock, g_pool, 64);
    Recorder rec;

    SubscribeOptions opts;
    opts.handler = record;
    opts.user = &rec;
    opts.capacity = 8;
    const SubscriptionId sub = bus.subscribe(opts);
    TEST_ASSERT_EQUAL_INT(0, sub);

    // Normal priority: telemetry posts of the same id would coalesce (covered
    // by test_telemetry_coalesces_same_id), so use Normal to keep all five.
    for (uint8_t i = 0; i < 5; ++i) {
        g_clock.now = 100 + i;
        TEST_ASSERT_TRUE(
            bus.post(EventId::SystemBoot, EventPriority::Normal, payload_with_byte(i)));
    }

    TEST_ASSERT_EQUAL_size_t(5, bus.pending(sub));
    TEST_ASSERT_EQUAL_size_t(5, bus.poll(sub, 10));
    TEST_ASSERT_EQUAL_size_t(5, rec.got.size());

    for (size_t i = 0; i < 5; ++i) {
        TEST_ASSERT_EQUAL_UINT8(i, rec.got[i].payload.data.bytes[0]);
        TEST_ASSERT_EQUAL_UINT32(100 + i, rec.got[i].timestamp_ms);
        TEST_ASSERT_EQUAL_UINT32(i + 1, rec.got[i].sequence);
    }
    TEST_ASSERT_EQUAL_size_t(0, bus.poll(sub, 10)); // drained
}

static void test_id_filter_and_wildcard(void)
{
    EventBus bus(g_lock, g_clock, g_pool, 64);
    Recorder rec_config;
    Recorder rec_all;

    SubscribeOptions config_opts;
    config_opts.id = EventId::ConfigChanged;
    config_opts.handler = record;
    config_opts.user = &rec_config;
    config_opts.capacity = 4;
    const SubscriptionId config_sub = bus.subscribe(config_opts);

    SubscribeOptions all_opts;
    all_opts.handler = record;
    all_opts.user = &rec_all;
    all_opts.capacity = 8;
    const SubscriptionId all_sub = bus.subscribe(all_opts);

    bus.post(EventId::ConfigChanged, EventPriority::Normal);
    bus.post(EventId::SystemBoot, EventPriority::Normal);

    TEST_ASSERT_EQUAL_size_t(1, bus.poll(config_sub, 4)); // only its id
    TEST_ASSERT_EQUAL_size_t(1, rec_config.got.size());
    TEST_ASSERT_EQUAL_INT(static_cast<int>(EventId::ConfigChanged),
                          static_cast<int>(rec_config.got[0].id));

    TEST_ASSERT_EQUAL_size_t(2, bus.poll(all_sub, 4)); // wildcard sees all
    TEST_ASSERT_EQUAL_size_t(2, rec_all.got.size());
}

static void test_min_priority_filter(void)
{
    EventBus bus(g_lock, g_clock, g_pool, 64);
    Recorder critical_only;
    Recorder everything;

    SubscribeOptions critical_opts;
    critical_opts.min_priority = EventPriority::Critical;
    critical_opts.handler = record;
    critical_opts.user = &critical_only;
    critical_opts.capacity = 4;
    const SubscriptionId crit_sub = bus.subscribe(critical_opts);

    SubscribeOptions all_opts;
    all_opts.handler = record;
    all_opts.user = &everything;
    all_opts.capacity = 4;
    const SubscriptionId all_sub = bus.subscribe(all_opts);

    bus.post(EventId::DiagnosticsTick, EventPriority::Telemetry);
    bus.post(EventId::SystemBoot, EventPriority::Normal);
    bus.post(EventId::HardwareFault, EventPriority::Critical);

    bus.poll(crit_sub, 4);
    TEST_ASSERT_EQUAL_size_t(1, critical_only.got.size());
    TEST_ASSERT_EQUAL_INT(static_cast<int>(EventId::HardwareFault),
                          static_cast<int>(critical_only.got[0].id));

    bus.poll(all_sub, 4);
    TEST_ASSERT_EQUAL_size_t(3, everything.got.size());
}

static void test_normal_drops_are_counted_when_full(void)
{
    EventBus bus(g_lock, g_clock, g_pool, 64);
    Recorder rec;

    SubscribeOptions opts;
    opts.handler = record;
    opts.user = &rec;
    opts.capacity = 2;
    const SubscriptionId sub = bus.subscribe(opts);

    TEST_ASSERT_TRUE(bus.post(EventId::SystemBoot, EventPriority::Normal));
    TEST_ASSERT_TRUE(bus.post(EventId::SystemShutdown, EventPriority::Normal));
    TEST_ASSERT_FALSE(bus.post(EventId::StorageError, EventPriority::Normal)); // full

    TEST_ASSERT_EQUAL_size_t(2, bus.pending(sub));
    const EventBusStats &stats = bus.stats();
    TEST_ASSERT_EQUAL_UINT32(1, stats.dropped[static_cast<int>(EventPriority::Normal)]);
    TEST_ASSERT_EQUAL_UINT32(2, stats.posted);
    // The third post HAD a route (a matching subscription) — it was a
    // queue drop, not "no route".
    TEST_ASSERT_EQUAL_UINT32(0, stats.post_rejected_no_route);
}

static void test_critical_evicts_oldest_lower_priority(void)
{
    EventBus bus(g_lock, g_clock, g_pool, 64);
    Recorder rec;

    SubscribeOptions opts;
    opts.handler = record;
    opts.user = &rec;
    opts.capacity = 2;
    const SubscriptionId sub = bus.subscribe(opts);

    bus.post(EventId::DiagnosticsTick, EventPriority::Telemetry); // oldest
    bus.post(EventId::SystemBoot, EventPriority::Normal);
    TEST_ASSERT_TRUE(bus.post(EventId::HardwareFault, EventPriority::Critical)); // full → evict

    TEST_ASSERT_EQUAL_size_t(2, bus.pending(sub));
    TEST_ASSERT_EQUAL_UINT32(1, bus.stats().evicted_by_critical);
    TEST_ASSERT_EQUAL_UINT32(0, bus.stats().dropped[static_cast<int>(EventPriority::Critical)]);

    bus.poll(sub, 4);
    TEST_ASSERT_EQUAL_size_t(2, rec.got.size());
    TEST_ASSERT_EQUAL_INT(static_cast<int>(EventId::SystemBoot),
                          static_cast<int>(rec.got[0].id)); // telemetry evicted
    TEST_ASSERT_EQUAL_INT(static_cast<int>(EventId::HardwareFault),
                          static_cast<int>(rec.got[1].id)); // critical queued
}

static void test_critical_never_silently_dropped(void)
{
    EventBus bus(g_lock, g_clock, g_pool, 64);
    Recorder rec;

    SubscribeOptions opts;
    opts.handler = record;
    opts.user = &rec;
    opts.capacity = 1;
    const SubscriptionId sub = bus.subscribe(opts);

    bus.post(EventId::HardwareFault, EventPriority::Critical);
    TEST_ASSERT_FALSE(bus.post(EventId::PeripheralOffline, EventPriority::Critical)); // all-critical queue

    // Observable, not silent: counted under Critical.
    TEST_ASSERT_EQUAL_UINT32(1, bus.stats().dropped[static_cast<int>(EventPriority::Critical)]);
    TEST_ASSERT_EQUAL_size_t(1, bus.pending(sub));
    TEST_ASSERT_EQUAL_size_t(1, bus.poll(sub, 4));
}

static void test_telemetry_coalesces_same_id(void)
{
    EventBus bus(g_lock, g_clock, g_pool, 64);
    Recorder rec;

    SubscribeOptions opts;
    opts.handler = record;
    opts.user = &rec;
    opts.capacity = 4;
    const SubscriptionId sub = bus.subscribe(opts);

    bus.post(EventId::LightStateChanged, EventPriority::Telemetry, payload_with_byte(1));
    bus.post(EventId::LightStateChanged, EventPriority::Telemetry, payload_with_byte(2));
    bus.post(EventId::DiagnosticsTick, EventPriority::Telemetry, payload_with_byte(3));

    TEST_ASSERT_EQUAL_size_t(2, bus.pending(sub)); // merged, not queued twice
    TEST_ASSERT_EQUAL_UINT32(1, bus.stats().coalesced);

    bus.poll(sub, 4);
    TEST_ASSERT_EQUAL_size_t(2, rec.got.size());
    TEST_ASSERT_EQUAL_INT(static_cast<int>(EventId::LightStateChanged),
                          static_cast<int>(rec.got[0].id));
    TEST_ASSERT_EQUAL_UINT8(2, rec.got[0].payload.data.bytes[0]); // newest payload won
}

static void test_post_without_route_is_rejected_and_counted(void)
{
    EventBus bus(g_lock, g_clock, g_pool, 64);

    TEST_ASSERT_FALSE(bus.post(EventId::SystemBoot, EventPriority::Normal));
    TEST_ASSERT_EQUAL_UINT32(1, bus.stats().post_rejected_no_route);
    TEST_ASSERT_EQUAL_UINT32(0, bus.stats().posted);
}

static void test_wildcard_subscription_cannot_receive_itself(void)
{
    EventBus bus(g_lock, g_clock, g_pool, 64);
    TEST_ASSERT_FALSE(bus.post(EventId::Any, EventPriority::Critical));
}

static void test_pool_exhaustion_is_bounded_and_unsub_releases_storage(void)
{
    EventBus bus(g_lock, g_clock, g_pool, 8); // pool fits only 8 events
    Recorder rec_a;
    Recorder rec_b;

    SubscribeOptions a;
    a.handler = record;
    a.user = &rec_a;
    a.capacity = 6;
    const SubscriptionId sub_a = bus.subscribe(a);

    SubscribeOptions b;
    b.handler = record;
    b.user = &rec_b;
    b.capacity = 6; // 6 + 6 > 8 → exhausted
    TEST_ASSERT_EQUAL_INT(lumen::kInvalidSubscription, bus.subscribe(b));

    TEST_ASSERT_TRUE(bus.unsubscribe(sub_a));
    // Pool storage is compacted and reusable after unsubscribe.
    const SubscriptionId sub_b = bus.subscribe(b);
    TEST_ASSERT_NOT_EQUAL(lumen::kInvalidSubscription, sub_b);

    bus.post(EventId::SystemBoot, EventPriority::Normal);
    TEST_ASSERT_EQUAL_size_t(1, bus.poll(sub_b, 4));
    TEST_ASSERT_EQUAL_size_t(1, rec_b.got.size());
}

static void test_event_size_is_bounded(void)
{
    TEST_ASSERT_TRUE(sizeof(Event) <= 32);
    TEST_ASSERT_TRUE(sizeof(EventPayload) <= 16);
}

static void test_handler_may_post_reentrantly(void)
{
    // Handlers run without the lock — a handler posting back into the bus
    // must not deadlock or drop (contract: lock not held during dispatch).
    EventBus bus(g_lock, g_clock, g_pool, 64);
    struct Ctx {
        EventBus *bus;
        int posts_made = 0;
    } ctx{&bus, 0};

    auto handler = [](const Event &event, void *user) {
        auto *c = static_cast<Ctx *>(user);
        if (event.id == EventId::SystemBoot && c->posts_made == 0) {
            c->posts_made++;
            c->bus->post(EventId::SystemShutdown, EventPriority::Normal);
        }
    };

    SubscribeOptions opts;
    opts.handler = handler;
    opts.user = &ctx;
    opts.capacity = 4;
    const SubscriptionId sub = bus.subscribe(opts);

    bus.post(EventId::SystemBoot, EventPriority::Normal);
    // Dispatch ONE event: the handler re-posts SystemShutdown while the lock
    // is not held; it must land in the queue rather than deadlock or drop.
    TEST_ASSERT_EQUAL_size_t(1, bus.poll(sub, 1));
    TEST_ASSERT_EQUAL_INT(1, ctx.posts_made);
    TEST_ASSERT_EQUAL_size_t(1, bus.poll(sub, 4)); // and it arrives
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_subscribe_rejects_invalid_options);
    RUN_TEST(test_fifo_ordering_and_sequence);
    RUN_TEST(test_id_filter_and_wildcard);
    RUN_TEST(test_min_priority_filter);
    RUN_TEST(test_normal_drops_are_counted_when_full);
    RUN_TEST(test_critical_evicts_oldest_lower_priority);
    RUN_TEST(test_critical_never_silently_dropped);
    RUN_TEST(test_telemetry_coalesces_same_id);
    RUN_TEST(test_post_without_route_is_rejected_and_counted);
    RUN_TEST(test_wildcard_subscription_cannot_receive_itself);
    RUN_TEST(test_pool_exhaustion_is_bounded_and_unsub_releases_storage);
    RUN_TEST(test_event_size_is_bounded);
    RUN_TEST(test_handler_may_post_reentrantly);
    return UNITY_END();
}
