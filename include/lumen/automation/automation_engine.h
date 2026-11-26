// Lumen — automation engine (SPEC §11). Pure C++ core, host-testable.
//
//   EVENT -> trigger matcher -> condition evaluator -> action executor -> history
//
// Safety (SPEC §11.8): bounded event queue and run table, per-automation
// cooldown and hourly rate limit, single-run mode (allow_parallel=false),
// self-trigger suppression and chain-depth loop detection via command
// origins ("auto:<id>:<depth>"), recursion limit for execute_automation,
// maximum run duration, cancellation, execution history ring.
//
// Threading: all methods must be called with lumen::model_mutex() held
// (the lighting task drives tick(); API handlers edit automations).
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "lumen/automation/automation.h"
#include "lumen/lighting/color_engine.h"
#include "lumen/lighting/transition.h"

namespace lumen
{

// Services the engine needs; the controller implements them on target and
// tests implement them with fakes.
class IAutomationHost
{
  public:
    virtual ~IAutomationHost() = default;
    virtual bool light_state(const std::string &id, LightState &out) = 0;
    virtual bool input_state(const std::string &id, bool &out) = 0;
    virtual bool exists(const char *kind, const std::string &id) = 0; // light|input|scene|group|room|output
    virtual bool light_command(const LightCommand &cmd) = 0;
    virtual bool scene_activate(const std::string &id, const std::string &origin) = 0;
    virtual bool group_command(const std::string &id, bool is_room, const LightCommand &cmd) = 0;
    virtual bool output_toggle(const std::string &output_id) = 0;
    virtual bool mqtt_publish(const std::string &topic, const std::string &payload, bool retain) = 0;
    virtual bool udp_send(const std::string &host, uint16_t port, const std::string &payload) = 0;
    virtual bool tcp_send(const std::string &host, uint16_t port, const std::string &payload) = 0;
};

enum class AutoEventType : uint8_t
{
    kLight = 0,
    kInput,
    kScene,
    kGroup,
    kStartup,
    kMqtt,
    kEspNow,
};

struct AutoEvent
{
    AutoEventType type = AutoEventType::kStartup;
    std::string id;      // light/input/scene/group id, MQTT topic, peer event
    bool state = false;  // input level / light power
    LightState light{};  // new light state (kLight)
    LightState prev{};   // previous light state (kLight)
    std::string payload; // MQTT payload
    std::string origin;  // command origin that caused it ("rest", "auto:x:1", ...)
    uint64_t posted_us = 0; // stamped by post() when a clock is set
};

// Local wall-clock context supplied each tick (valid only once SNTP synced).
struct AutoClock
{
    bool valid = false;
    int64_t unix_time = 0;
    int minute_of_day = 0; // 0..1439
    int weekday = 0;       // 0 = Monday .. 6 = Sunday
    int day_id = 0;        // changes once per day (e.g. year*400+yday)
    int sunrise_min = -1;  // minutes of day, -1 if unknown (no location)
    int sunset_min = -1;
};

struct HistoryEntry
{
    std::string automation_id;
    std::string trigger;     // what fired it
    int64_t unix_time = 0;   // 0 when the clock is not synced
    uint32_t uptime_ms = 0;
    uint32_t duration_ms = 0;
    std::string result;      // ok | error | skipped | cancelled | loop | rate_limited
    std::string message;
};

struct AutomationRuntime
{
    bool running = false;
    uint32_t last_run_uptime_ms = 0;
    int64_t last_run_unix = 0;
    std::string last_result;
    uint32_t runs = 0;
    uint32_t errors = 0;
};

class AutomationEngine
{
  public:
    static constexpr std::size_t kMaxEvents = 32;
    static constexpr std::size_t kMaxRuns = 8;
    static constexpr std::size_t kHistory = 32;
    static constexpr std::size_t kMaxVariables = 32;
    static constexpr uint8_t kMaxChainDepth = 4;
    static constexpr uint32_t kMaxRunMs = 15U * 60U * 1000U;
    static constexpr uint32_t kDoublePressWindowMs = 400;
    static constexpr uint32_t kLongPressMs = 800;

    explicit AutomationEngine(IAutomationHost &host) : host_(host) {}

    // --- definitions ------------------------------------------------------
    void clear();
    bool upsert(const Automation &a);   // replaces by id; false when full
    bool remove(const std::string &id); // cancels running instances
    bool set_enabled(const std::string &id, bool enabled);
    const Automation *find(const std::string &id) const;
    const std::vector<Automation> &automations() const { return automations_; }
    const AutomationRuntime *runtime(const std::string &id) const;

    // Reference + cycle validation before saving/enabling (SPEC §11.8).
    bool validate(const Automation &a, std::string &error) const;

    // --- execution --------------------------------------------------------
    bool post(const AutoEvent &e);   // bounded; false when the queue is full
    void tick(uint32_t now_ms, const AutoClock &clock);
    // Run now regardless of trigger/conditions (UI "Test"); still subject to
    // loop/recursion limits. Returns false when unknown or the run table is full.
    bool run_now(const std::string &id, uint32_t now_ms, const AutoClock &clock, std::string &error);
    void cancel(const std::string &id, uint32_t now_ms);

    // --- observability ----------------------------------------------------
    std::vector<HistoryEntry> history(const std::string &id = "") const;
    const std::map<std::string, double> &variables() const { return variables_; }
    bool set_variable(const std::string &name, double value);
    uint32_t dropped_events() const { return dropped_events_; }
    // Event wait (post -> dispatch), microseconds; clock optional (host tests).
    void set_clock_us(uint64_t (*clock)()) { clock_us_ = clock; }
    uint32_t event_latency_last_us() const { return ev_lat_last_; }
    uint32_t event_latency_max_us() const { return ev_lat_max_; }

    using ExecutedListener = void (*)(const HistoryEntry &entry, void *user);
    void set_listener(ExecutedListener l, void *user)
    {
        listener_ = l;
        listener_user_ = user;
    }

  private:
    struct Run
    {
        bool active = false;
        std::string id;
        std::size_t pc = 0;
        uint32_t resume_at = 0;
        uint32_t started = 0;
        uint8_t depth = 0;
        std::string trigger;
        int64_t unix_start = 0;
    };
    struct Rate
    {
        uint32_t last_start = 0;
        bool has_started = false;
        uint32_t window_start = 0;
        uint16_t in_window = 0;
        int last_schedule_key = -1; // day*1440+minute already fired
        uint32_t next_timer_ms = 0;
    };
    struct Button
    {
        bool down = false;
        uint32_t down_at = 0;
        bool long_fired = false;
        uint8_t presses = 0;
        uint32_t decide_at = 0; // pending single/double decision
    };

    void dispatch(const AutoEvent &e, uint32_t now_ms, const AutoClock &clock);
    void fire(std::size_t index, const std::string &trigger, uint8_t depth, uint32_t now_ms,
              const AutoClock &clock, bool check_conditions);
    bool eval(const Condition &c, const AutoClock &clock) const;
    bool compare(double lhs, CompareOp op, double rhs) const;
    double light_field(const LightState &s, const std::string &field) const;
    void step_run(Run &run, uint32_t now_ms, const AutoClock &clock);
    void finish(Run &run, uint32_t now_ms, const char *result, const std::string &message);
    void record(const HistoryEntry &h);
    bool uses_double_press(const std::string &input) const;
    void button_fire(const std::string &input, TriggerType kind, uint32_t now_ms, const AutoClock &clock);
    std::size_t index_of(const std::string &id) const;
    bool reaches(const std::string &from, const std::string &target, int budget) const;

    IAutomationHost &host_;
    std::vector<Automation> automations_;
    std::vector<AutomationRuntime> runtime_;
    std::vector<Rate> rate_;
    std::array<AutoEvent, kMaxEvents> events_{};
    std::size_t ev_head_ = 0;
    std::size_t ev_count_ = 0;
    std::array<Run, kMaxRuns> runs_{};
    std::array<HistoryEntry, kHistory> history_{};
    std::size_t hist_next_ = 0;
    std::size_t hist_count_ = 0;
    std::map<std::string, double> variables_;
    std::map<std::string, Button> buttons_;
    bool started_ = false;
    uint32_t dropped_events_ = 0;
    uint64_t (*clock_us_)() = nullptr;
    uint32_t ev_lat_last_ = 0, ev_lat_max_ = 0;
    ExecutedListener listener_ = nullptr;
    void *listener_user_ = nullptr;
};

// NOAA sunrise/sunset for a date and location. Minutes after local
// midnight (utc_offset_min = local - UTC). Returns false at the poles when
// the sun does not rise/set that day.
bool sun_times(int year, int month, int day, double latitude, double longitude,
               int utc_offset_min, int &sunrise_min, int &sunset_min);

} // namespace lumen
