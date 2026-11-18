// Lumen event bus implementation — see lumen/core/event_bus.hpp for the
// contract. Pure C++ (no ESP-IDF, no heap): the lock/clock ports are the
// only platform surface, which keeps every rule host-testable.
#include "lumen/core/event_bus.hpp"

namespace lumen
{

EventBus::EventBus(ILock &lock, IClock &clock, Event *pool, std::size_t pool_events)
    : lock_(lock), clock_(clock), pool_(pool), pool_events_(pool_events), pool_used_(0),
      sequence_(0), stats_(EventBusStats{})
{
}

SubscriptionId EventBus::subscribe(const SubscribeOptions &options)
{
    if (options.handler == nullptr || options.capacity == 0 || pool_ == nullptr) {
        return kInvalidSubscription;
    }

    lock_.lock();

    SubscriptionId slot = kInvalidSubscription;
    for (std::size_t i = 0; i < subs_.size(); ++i) {
        if (!subs_[i].active) {
            slot = static_cast<SubscriptionId>(i);
            break;
        }
    }

    if (slot == kInvalidSubscription || pool_used_ + options.capacity > pool_events_) {
        lock_.unlock();
        return kInvalidSubscription;
    }

    Subscription &sub = subs_[static_cast<std::size_t>(slot)];
    sub.events = pool_ + pool_used_;
    pool_used_ += options.capacity;
    sub.capacity = options.capacity;
    sub.head = 0;
    sub.count = 0;
    sub.id = options.id;
    sub.min_priority = options.min_priority;
    sub.handler = options.handler;
    sub.user = options.user;
    sub.active = true;

    lock_.unlock();
    return slot;
}

bool EventBus::unsubscribe(SubscriptionId id)
{
    if (id < 0 || static_cast<std::size_t>(id) >= subs_.size()) {
        return false;
    }

    lock_.lock();
    Subscription &sub = subs_[static_cast<std::size_t>(id)];
    if (!sub.active) {
        lock_.unlock();
        return false;
    }
    sub = Subscription{}; // releases the slice bookkeeping
    compact_locked();     // pool storage becomes reusable
    lock_.unlock();
    return true;
}

bool EventBus::post(Event event)
{
    if (!is_real_event(event.id)) {
        return false;
    }

    lock_.lock();
    event.timestamp_ms = clock_.ms();
    event.sequence = ++sequence_;
    bool matched = false;
    bool accepted = false;
    route_locked(event, matched, accepted);
    if (accepted) {
        stats_.posted++;
    } else if (!matched) {
        // A full queue is already counted per-priority by push_locked();
        // only "nobody subscribes to this" counts as unrouted.
        stats_.post_rejected_no_route++;
    }
    lock_.unlock();
    return accepted;
}

bool EventBus::post(EventId id, EventPriority priority, const EventPayload &payload)
{
    Event event;
    event.id = id;
    event.priority = priority;
    event.payload = payload;
    return post(event);
}

std::size_t EventBus::poll(SubscriptionId id, std::size_t max_events)
{
    if (id < 0 || static_cast<std::size_t>(id) >= subs_.size() || max_events == 0) {
        return 0;
    }

    std::size_t dispatched = 0;
    while (dispatched < max_events) {
        Event event;
        EventHandler handler = nullptr;
        void *user = nullptr;
        lock_.lock();
        Subscription &sub = subs_[static_cast<std::size_t>(id)];
        if (!sub.active || sub.count == 0) {
            lock_.unlock();
            break;
        }
        event = sub.events[sub.head];
        sub.head = (sub.head + 1) % sub.capacity;
        sub.count--;
        stats_.poll_delivered++;
        handler = sub.handler;
        user = sub.user;
        lock_.unlock();

        // Handlers run WITHOUT the lock (they may post()).
        handler(event, user);
        dispatched++;
    }
    return dispatched;
}

std::size_t EventBus::pending(SubscriptionId id) const
{
    if (id < 0 || static_cast<std::size_t>(id) >= subs_.size()) {
        return 0;
    }
    lock_.lock();
    const std::size_t count = subs_[static_cast<std::size_t>(id)].count;
    lock_.unlock();
    return count;
}

void EventBus::route_locked(const Event &event, bool &matched, bool &accepted)
{
    for (Subscription &sub : subs_) {
        if (!sub.active) {
            continue;
        }
        if (sub.id != EventId::Any && sub.id != event.id) {
            continue;
        }
        if (event.priority < sub.min_priority) {
            continue;
        }
        matched = true;
        accepted = push_locked(sub, event) || accepted;
    }
}

bool EventBus::push_locked(Subscription &sub, const Event &event)
{
    // Telemetry coalescing: merge with an already-queued event of the same
    // id (newest match first) instead of growing the queue.
    if (event.priority == EventPriority::Telemetry && sub.count > 0) {
        for (std::size_t step = 1; step <= sub.count; ++step) {
            const std::size_t idx = (sub.head + sub.count - step) % sub.capacity;
            if (sub.events[idx].id == event.id) {
                sub.events[idx] = event;
                stats_.coalesced++;
                return true;
            }
        }
    }

    if (sub.count < sub.capacity) {
        sub.events[(sub.head + sub.count) % sub.capacity] = event;
        sub.count++;
        return true;
    }

    // Queue full.
    if (event.priority == EventPriority::Critical) {
        // Never silently dropped: evict the OLDEST lower-priority event.
        for (std::size_t step = 0; step < sub.count; ++step) {
            const std::size_t idx = (sub.head + step) % sub.capacity;
            if (sub.events[idx].priority >= EventPriority::Critical) {
                continue;
            }
            // Shift newer entries one slot toward the head (bounded by
            // capacity; O(capacity) worst case under a short critical section).
            for (std::size_t j = step; j + 1 < sub.count; ++j) {
                const std::size_t from = (sub.head + j + 1) % sub.capacity;
                const std::size_t to = (sub.head + j) % sub.capacity;
                sub.events[to] = sub.events[from];
            }
            sub.count--;
            stats_.evicted_by_critical++;
            sub.events[(sub.head + sub.count) % sub.capacity] = event;
            sub.count++;
            return true;
        }
        // Queue holds only critical events — observable drop, never silent.
        stats_drop_locked(EventPriority::Critical);
        return false;
    }

    stats_drop_locked(event.priority);
    return false;
}

void EventBus::compact_locked()
{
    // Rebuild slices in slot order so unsubscribed storage is reusable.
    std::size_t used = 0;
    for (Subscription &sub : subs_) {
        if (!sub.active) {
            continue;
        }
        Event *slice = pool_ + used;
        if (slice != sub.events && sub.count > 0) {
            // Move queued events into the compacted slice (same relative
            // order: head-first).
            for (std::size_t i = 0; i < sub.count; ++i) {
                slice[i] = sub.events[(sub.head + i) % sub.capacity];
            }
        }
        sub.events = slice;
        sub.head = 0;
        used += sub.capacity;
    }
    pool_used_ = used;
}

} // namespace lumen
