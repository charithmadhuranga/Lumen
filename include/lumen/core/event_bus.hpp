// Lumen event bus — typed, bounded, publish/subscribe (SPEC §23 rules 4–6).
//
// Design contract:
// - Producers NEVER block: post() takes a short critical section only.
// - Each subscription owns a fixed slice of caller-provided pool storage;
//   the bus performs no heap allocation at all.
// - Drop policy is explicit and observable via EventBusStats:
//     * Critical  : never silently dropped — evicts the OLDEST event of
//                   lower priority; if the queue holds only critical events
//                   the drop is counted (dropped[Critical]) and reported.
//     * Normal     : dropped when full → counted.
//     * Telemetry : coalesces with an already-queued event of the same id;
//                   dropped when full → counted.
// - Delivery happens when the owner polls its subscription, so handlers run
//   in the consumer's context, not the producer's (AGENTS.md §4 rule 7).
// - A subscription may be polled by exactly one task at a time. Handlers may
//   freely post() (the lock is not held while handlers run).
//
// No ESP-IDF primitives here — ILock/IClock are injected ports (see
// lumen/core/ports.hpp), so the whole bus is host-testable.
#pragma once

#include <cstddef>
#include <cstdint>

#include "lumen/core/event.hpp"
#include "lumen/core/ports.hpp"

namespace lumen
{

using SubscriptionId = int16_t;
constexpr SubscriptionId kInvalidSubscription = -1;

using EventHandler = void (*)(const Event &event, void *user);

struct SubscribeOptions {
    EventId id = EventId::Any; // EventId::Any = wildcard subscription
    EventPriority min_priority = EventPriority::Telemetry;
    std::size_t capacity = 8; // events buffered (slice size, bounded)
    EventHandler handler = nullptr;
    void *user = nullptr;
};

struct EventBusStats {
    uint32_t posted = 0;                 // accepted into ≥ 1 queue
    uint32_t poll_delivered = 0;         // events dispatched to handlers
    uint32_t dropped[3] = {};            // indexed by EventPriority
    uint32_t evicted_by_critical = 0;    // lower-priority events displaced
    uint32_t coalesced = 0;              // telemetry merges
    uint32_t post_rejected_no_route = 0; // no matching subscription
};

class EventBus
{
  public:
    // `pool` is caller-owned storage split into per-subscription slices;
    // `pool_events` bounds total buffered events; `max_subscriptions`
    // bounds routing table growth. All allocation happens in subscribe().
    EventBus(ILock &lock, IClock &clock, Event *pool, std::size_t pool_events,
             std::size_t max_subscriptions);

    // Registers a subscription; returns kInvalidSubscription when the
    // routing table or pool is exhausted (never grows unbounded).
    SubscriptionId subscribe(const SubscribeOptions &options);

    // Drops pending events and releases the slice. Returns false if the id
    // is unknown.
    bool unsubscribe(SubscriptionId id);

    // Routes an event to every matching subscription without blocking.
    // Returns true when at least one queue accepted it.
    bool post(Event event);

    // Convenience overload: builds the event and posts it.
    bool post(EventId id, EventPriority priority, const EventPayload &payload = {});

    // Dispatches up to `max_events` pending events of one subscription to
    // its handler. Returns how many were dispatched.
    std::size_t poll(SubscriptionId id, std::size_t max_events);

    std::size_t pending(SubscriptionId id) const;

    const EventBusStats &stats() const
    {
        return stats_;
    }

    void reset_stats()
    {
        stats_ = EventBusStats{};
    }

  private:
    struct Subscription {
        Event *events = nullptr; // slice into pool
        std::size_t capacity = 0;
        std::size_t head = 0; // pop index
        std::size_t count = 0;
        EventId id = EventId::Any;
        EventPriority min_priority = EventPriority::Telemetry;
        EventHandler handler = nullptr;
        void *user = nullptr;
        bool active = false;
    };

    // All three run with the lock HELD.
    bool route_locked(const Event &event);
    bool push_locked(Subscription &sub, const Event &event);
    void stats_drop_locked(EventPriority priority)
    {
        stats_.dropped[static_cast<std::size_t>(priority)]++;
    }

    ILock &lock_;
    IClock &clock_;
    Event *pool_;
    std::size_t pool_events_;
    std::size_t pool_used_;
    Subscription *subs_;
    std::size_t max_subs_;
    std::size_t sub_count_;
    uint32_t sequence_;
    EventBusStats stats_;
};

} // namespace lumen
