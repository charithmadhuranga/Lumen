// Lumen — automation engine implementation. See automation_engine.h
#include "lumen/automation/automation_engine.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace lumen
{

namespace
{

constexpr std::size_t kActionsPerTick = 16;

// "auto:<id>:<depth>" -> depth (0 when the origin is not an automation).
uint8_t origin_depth(const std::string &origin, std::string *auto_id = nullptr)
{
    if (origin.rfind("auto:", 0) != 0) return 0;
    const std::size_t colon = origin.rfind(':');
    if (colon <= 5) return 1;
    if (auto_id != nullptr) *auto_id = origin.substr(5, colon - 5);
    return static_cast<uint8_t>(std::atoi(origin.c_str() + colon + 1));
}

std::string origin_for(const std::string &id, uint8_t depth)
{
    return "auto:" + id + ":" + std::to_string(depth);
}

} // namespace

// ---------------------------------------------------------------------------
// Definitions
// ---------------------------------------------------------------------------

std::size_t AutomationEngine::index_of(const std::string &id) const
{
    for (std::size_t i = 0; i < automations_.size(); ++i) {
        if (automations_[i].id == id) return i;
    }
    return automations_.size();
}

void AutomationEngine::clear()
{
    automations_.clear();
    runtime_.clear();
    rate_.clear();
    for (auto &r : runs_) r = Run{};
}

bool AutomationEngine::upsert(const Automation &a)
{
    const std::size_t i = index_of(a.id);
    if (i < automations_.size()) {
        automations_[i] = a;
        rate_[i].last_schedule_key = -1;
        rate_[i].next_timer_ms = 0;
        return true;
    }
    if (automations_.size() >= kMaxAutomations) return false;
    automations_.push_back(a);
    runtime_.push_back({});
    rate_.push_back({});
    return true;
}

bool AutomationEngine::remove(const std::string &id)
{
    const std::size_t i = index_of(id);
    if (i >= automations_.size()) return false;
    for (auto &r : runs_) {
        if (r.active && r.id == id) r.active = false;
    }
    automations_.erase(automations_.begin() + static_cast<long>(i));
    runtime_.erase(runtime_.begin() + static_cast<long>(i));
    rate_.erase(rate_.begin() + static_cast<long>(i));
    return true;
}

bool AutomationEngine::set_enabled(const std::string &id, bool enabled)
{
    const std::size_t i = index_of(id);
    if (i >= automations_.size()) return false;
    automations_[i].enabled = enabled;
    if (!enabled) {
        for (auto &r : runs_) {
            if (r.active && r.id == id) r.active = false;
        }
        runtime_[i].running = false;
    }
    return true;
}

const Automation *AutomationEngine::find(const std::string &id) const
{
    const std::size_t i = index_of(id);
    return i < automations_.size() ? &automations_[i] : nullptr;
}

const AutomationRuntime *AutomationEngine::runtime(const std::string &id) const
{
    const std::size_t i = index_of(id);
    return i < runtime_.size() ? &runtime_[i] : nullptr;
}

bool AutomationEngine::reaches(const std::string &from, const std::string &target, int budget) const
{
    if (budget <= 0) return true; // too deep: treat as a cycle
    const Automation *a = find(from);
    if (a == nullptr) return false;
    for (const auto &ac : a->actions) {
        if (ac.type != ActionType::kExecuteAutomation) continue;
        if (ac.target == target || reaches(ac.target, target, budget - 1)) return true;
    }
    return false;
}

bool AutomationEngine::validate(const Automation &a, std::string &error) const
{
    const Trigger &t = a.trigger;
    auto need = [&](const char *kind, const std::string &id, const char *what) {
        if (!host_.exists(kind, id)) {
            error = std::string(what) + " '" + id + "' does not exist";
            return false;
        }
        return true;
    };
    switch (t.type) {
    case TriggerType::kLightState:
    case TriggerType::kBrightnessChange:
    case TriggerType::kColorChange:
        if (!need("light", t.target, "Trigger light")) return false;
        break;
    case TriggerType::kInputState:
    case TriggerType::kButtonPress:
    case TriggerType::kDoublePress:
    case TriggerType::kLongPress:
        if (!need("input", t.target, "Trigger input")) return false;
        break;
    case TriggerType::kSceneActivated:
        if (!need("scene", t.target, "Trigger scene")) return false;
        break;
    case TriggerType::kGroupStateChanged:
        if (!need("group", t.target, "Trigger group")) return false;
        break;
    default:
        break;
    }
    for (std::size_t i = 0; i < a.actions.size(); ++i) {
        const Action &ac = a.actions[i];
        const std::string where = "Action " + std::to_string(i + 1) + ": ";
        bool ok = true;
        switch (ac.type) {
        case ActionType::kLightOn:
        case ActionType::kLightOff:
        case ActionType::kLightToggle:
        case ActionType::kLightBrightness:
        case ActionType::kLightColor:
        case ActionType::kLightCct:
        case ActionType::kEffectRun:
        case ActionType::kEffectStop:
            ok = need("light", ac.target, "light");
            break;
        case ActionType::kSceneActivate:
            ok = need("scene", ac.target, "scene");
            break;
        case ActionType::kGroupControl:
            ok = need("group", ac.target, "group");
            break;
        case ActionType::kRoomControl:
            ok = need("room", ac.target, "room");
            break;
        case ActionType::kRelayToggle:
            ok = host_.exists("light", ac.target) || need("output", ac.target, "output");
            break;
        case ActionType::kExecuteAutomation:
            if (ac.target == a.id) {
                error = where + "an automation cannot run itself";
                return false;
            }
            if (find(ac.target) == nullptr) {
                error = where + "automation '" + ac.target + "' does not exist";
                return false;
            }
            if (reaches(ac.target, a.id, kMaxChainDepth + 1)) {
                error = where + "running '" + ac.target + "' would loop back to this automation";
                return false;
            }
            break;
        default:
            break;
        }
        if (!ok) {
            error = where + error;
            return false;
        }
    }
    return true;
}

// ---------------------------------------------------------------------------
// Events
// ---------------------------------------------------------------------------

bool AutomationEngine::post(const AutoEvent &e)
{
    if (ev_count_ >= kMaxEvents) {
        ++dropped_events_;
        return false;
    }
    events_[(ev_head_ + ev_count_) % kMaxEvents] = e;
    ++ev_count_;
    return true;
}

bool AutomationEngine::uses_double_press(const std::string &input) const
{
    for (const auto &a : automations_) {
        if (a.enabled && a.trigger.type == TriggerType::kDoublePress && a.trigger.target == input) return true;
    }
    return false;
}

void AutomationEngine::button_fire(const std::string &input, TriggerType kind, uint32_t now_ms,
                                   const AutoClock &clock)
{
    const char *label = kind == TriggerType::kLongPress     ? "long press "
                        : kind == TriggerType::kDoublePress ? "double press "
                                                            : "press ";
    for (std::size_t i = 0; i < automations_.size(); ++i) {
        const Automation &a = automations_[i];
        if (a.enabled && a.trigger.type == kind && a.trigger.target == input) {
            fire(i, label + input, 0, now_ms, clock, true);
        }
    }
}

void AutomationEngine::dispatch(const AutoEvent &e, uint32_t now_ms, const AutoClock &clock)
{
    std::string cause_id;
    const uint8_t depth = origin_depth(e.origin, &cause_id);

    if (e.type == AutoEventType::kInput) {
        Button &b = buttons_[e.id];
        if (e.state && !b.down) {
            b.down = true;
            b.down_at = now_ms;
            b.long_fired = false;
        } else if (!e.state && b.down) {
            b.down = false;
            if (!b.long_fired) {
                b.presses++;
                if (uses_double_press(e.id)) {
                    if (b.presses >= 2) {
                        b.presses = 0;
                        b.decide_at = 0;
                        button_fire(e.id, TriggerType::kDoublePress, now_ms, clock);
                    } else {
                        b.decide_at = now_ms + kDoublePressWindowMs;
                    }
                } else {
                    b.presses = 0;
                    button_fire(e.id, TriggerType::kButtonPress, now_ms, clock);
                }
            }
        }
    }

    for (std::size_t i = 0; i < automations_.size(); ++i) {
        const Automation &a = automations_[i];
        if (!a.enabled) continue;
        const Trigger &t = a.trigger;
        bool match = false;
        std::string label;
        switch (e.type) {
        case AutoEventType::kLight:
            if (t.target != e.id) break;
            if (t.type == TriggerType::kLightState && e.light.power != e.prev.power) {
                match = !t.has_state || t.state == e.light.power;
                label = e.id + (e.light.power ? " on" : " off");
            } else if (t.type == TriggerType::kBrightnessChange && e.light.brightness != e.prev.brightness) {
                if (t.has_threshold) {
                    // Fire when the threshold condition becomes true.
                    match = compare(e.light.brightness, t.op, t.threshold) &&
                            !compare(e.prev.brightness, t.op, t.threshold);
                } else {
                    match = true;
                }
                label = e.id + " brightness " + std::to_string(e.light.brightness) + "%";
            } else if (t.type == TriggerType::kColorChange &&
                       (e.light.red != e.prev.red || e.light.green != e.prev.green ||
                        e.light.blue != e.prev.blue || e.light.color_temperature != e.prev.color_temperature)) {
                match = true;
                label = e.id + " colour";
            }
            break;
        case AutoEventType::kInput:
            if (t.type == TriggerType::kInputState && t.target == e.id) {
                match = !t.has_state || t.state == e.state;
                label = "input " + e.id + (e.state ? " on" : " off");
            }
            break;
        case AutoEventType::kScene:
            match = t.type == TriggerType::kSceneActivated && t.target == e.id;
            label = "scene " + e.id;
            break;
        case AutoEventType::kGroup:
            match = t.type == TriggerType::kGroupStateChanged && t.target == e.id;
            label = "group " + e.id;
            break;
        case AutoEventType::kStartup:
            match = t.type == TriggerType::kControllerStartup;
            label = "startup";
            break;
        case AutoEventType::kMqtt:
            match = t.type == TriggerType::kMqttEvent && t.target == e.id && (t.payload.empty() || t.payload == e.payload);
            label = "mqtt " + e.id;
            break;
        case AutoEventType::kEspNow:
            match = t.type == TriggerType::kEspNowEvent && (t.target.empty() || t.target == e.id);
            label = "peer event " + e.id;
            break;
        }
        if (!match) continue;
        // Self-trigger suppression: an automation never re-triggers itself
        // through its own light commands.
        if (!cause_id.empty() && cause_id == a.id) continue;
        if (depth >= kMaxChainDepth) {
            HistoryEntry h;
            h.automation_id = a.id;
            h.trigger = label;
            h.unix_time = clock.valid ? clock.unix_time : 0;
            h.uptime_ms = now_ms;
            h.result = "loop";
            h.message = "Chain of automations triggering each other stopped after " +
                        std::to_string(kMaxChainDepth) + " steps";
            runtime_[i].errors++;
            runtime_[i].last_result = "loop";
            record(h);
            continue;
        }
        fire(i, label, static_cast<uint8_t>(depth), now_ms, clock, true);
    }
}

// ---------------------------------------------------------------------------
// Firing, conditions, actions
// ---------------------------------------------------------------------------

bool AutomationEngine::compare(double lhs, CompareOp op, double rhs) const
{
    switch (op) {
    case CompareOp::kEq: return std::fabs(lhs - rhs) < 1e-9;
    case CompareOp::kNe: return std::fabs(lhs - rhs) >= 1e-9;
    case CompareOp::kGt: return lhs > rhs;
    case CompareOp::kLt: return lhs < rhs;
    case CompareOp::kGe: return lhs >= rhs;
    case CompareOp::kLe: return lhs <= rhs;
    }
    return false;
}

double AutomationEngine::light_field(const LightState &s, const std::string &f) const
{
    if (f == "power") return s.power ? 1 : 0;
    if (f == "brightness") return s.power ? s.brightness : 0;
    if (f == "red") return s.red;
    if (f == "green") return s.green;
    if (f == "blue") return s.blue;
    if (f == "white") return s.white;
    if (f == "color_temperature") return s.color_temperature;
    return 0;
}

bool AutomationEngine::eval(const Condition &c, const AutoClock &clock) const
{
    switch (c.type) {
    case ConditionType::kAnd:
        for (const auto &i : c.items) {
            if (!eval(i, clock)) return false;
        }
        return true;
    case ConditionType::kOr:
        for (const auto &i : c.items) {
            if (eval(i, clock)) return true;
        }
        return c.items.empty();
    case ConditionType::kNot:
        return c.items.empty() || !eval(c.items.front(), clock);
    case ConditionType::kTimeRange: {
        if (!clock.valid) return false; // unknown time never satisfies a window
        const int m = clock.minute_of_day;
        return c.start_min <= c.end_min ? (m >= c.start_min && m < c.end_min)
                                        : (m >= c.start_min || m < c.end_min); // spans midnight
    }
    case ConditionType::kDayOfWeek:
        return clock.valid && (c.days & (1U << clock.weekday)) != 0;
    case ConditionType::kCompare: {
        double lhs = 0;
        if (c.source == "light") {
            LightState s{};
            if (!host_.light_state(c.id, s)) return false;
            lhs = light_field(s, c.field);
        } else if (c.source == "input") {
            bool v = false;
            if (!host_.input_state(c.id, v)) return false;
            lhs = v ? 1 : 0;
        } else {
            auto it = variables_.find(c.id);
            lhs = it == variables_.end() ? 0 : it->second;
        }
        return compare(lhs, c.op, c.value);
    }
    }
    return false;
}

void AutomationEngine::fire(std::size_t index, const std::string &trigger, uint8_t depth, uint32_t now_ms,
                            const AutoClock &clock, bool check_conditions)
{
    const Automation &a = automations_[index];
    AutomationRuntime &rt = runtime_[index];
    Rate &rate = rate_[index];
    HistoryEntry h;
    h.automation_id = a.id;
    h.trigger = trigger;
    h.unix_time = clock.valid ? clock.unix_time : 0;
    h.uptime_ms = now_ms;

    if (check_conditions && a.has_conditions && !eval(a.conditions, clock)) {
        return; // conditions not met: not an execution, no history noise
    }
    if (rate.has_started && now_ms - rate.last_start < a.execution.cooldown_ms) {
        return; // cooldown: silently ignored (debounce semantics)
    }
    if (now_ms - rate.window_start >= 3600000U || !rate.has_started) {
        rate.window_start = now_ms;
        rate.in_window = 0;
    }
    if (rate.in_window >= a.execution.max_per_hour) {
        h.result = "rate_limited";
        h.message = "Hourly execution limit (" + std::to_string(a.execution.max_per_hour) + ") reached";
        rt.last_result = h.result;
        record(h);
        return;
    }
    bool running = false;
    for (const auto &r : runs_) running |= r.active && r.id == a.id;
    if (running && !a.execution.allow_parallel) {
        h.result = "skipped";
        h.message = "Previous run still in progress";
        record(h);
        return;
    }
    Run *slot = nullptr;
    for (auto &r : runs_) {
        if (!r.active) {
            slot = &r;
            break;
        }
    }
    if (slot == nullptr) {
        h.result = "error";
        h.message = "Too many automations running at once";
        rt.errors++;
        rt.last_result = h.result;
        record(h);
        return;
    }
    rate.has_started = true;
    rate.last_start = now_ms;
    rate.in_window++;
    *slot = Run{};
    slot->active = true;
    slot->id = a.id;
    slot->started = now_ms;
    slot->resume_at = now_ms;
    slot->depth = static_cast<uint8_t>(depth + 1);
    slot->trigger = trigger;
    slot->unix_start = h.unix_time;
    rt.running = true;
    rt.runs++;
    rt.last_run_uptime_ms = now_ms;
    rt.last_run_unix = h.unix_time;
    step_run(*slot, now_ms, clock);
}

void AutomationEngine::finish(Run &run, uint32_t now_ms, const char *result, const std::string &message)
{
    HistoryEntry h;
    h.automation_id = run.id;
    h.trigger = run.trigger;
    h.unix_time = run.unix_start;
    h.uptime_ms = run.started;
    h.duration_ms = now_ms - run.started;
    h.result = result;
    h.message = message;
    const std::size_t i = index_of(run.id);
    run.active = false;
    if (i < runtime_.size()) {
        bool still = false;
        for (const auto &r : runs_) still |= r.active && r.id == run.id;
        runtime_[i].running = still;
        runtime_[i].last_result = result;
        if (h.result != "ok") runtime_[i].errors++;
    }
    record(h);
}

void AutomationEngine::step_run(Run &run, uint32_t now_ms, const AutoClock &clock)
{
    const Automation *a = find(run.id);
    if (a == nullptr) {
        run.active = false;
        return;
    }
    if (now_ms - run.started > kMaxRunMs) {
        finish(run, now_ms, "cancelled", "Exceeded the maximum run time");
        return;
    }
    std::size_t budget = kActionsPerTick;
    const std::string origin = origin_for(a->id, run.depth);
    while (run.active && run.pc < a->actions.size() && budget-- > 0) {
        if (static_cast<int32_t>(now_ms - run.resume_at) < 0) return; // delayed
        const Action &ac = a->actions[run.pc++];
        LightCommand cmd;
        cmd.channel_id = ac.target;
        cmd.origin = origin;
        cmd.transition_ms = ac.transition_ms;
        cmd.use_default_transition = !ac.has_transition;
        bool ok = true;
        std::string err;
        switch (ac.type) {
        case ActionType::kLightOn:
        case ActionType::kLightOff:
            cmd.state.power = ac.type == ActionType::kLightOn;
            cmd.fields = kFieldPower;
            if (ac.type == ActionType::kLightOn && ac.brightness >= 0) {
                cmd.state.brightness = static_cast<uint8_t>(ac.brightness);
                cmd.fields |= kFieldBrightness;
            }
            ok = host_.light_command(cmd);
            break;
        case ActionType::kLightToggle: {
            LightState s{};
            ok = host_.light_state(ac.target, s);
            cmd.state.power = !s.power;
            cmd.fields = kFieldPower;
            ok = ok && host_.light_command(cmd);
            break;
        }
        case ActionType::kLightBrightness:
            cmd.state.brightness = static_cast<uint8_t>(ac.brightness);
            cmd.fields = kFieldBrightness;
            ok = host_.light_command(cmd);
            break;
        case ActionType::kLightColor:
            cmd.state.red = ac.red;
            cmd.state.green = ac.green;
            cmd.state.blue = ac.blue;
            cmd.fields = kFieldColor;
            if (ac.brightness >= 0) {
                cmd.state.brightness = static_cast<uint8_t>(ac.brightness);
                cmd.fields |= kFieldBrightness;
            }
            ok = host_.light_command(cmd);
            break;
        case ActionType::kLightCct:
            cmd.state.color_temperature = ac.kelvin;
            cmd.fields = kFieldCct;
            ok = host_.light_command(cmd);
            break;
        case ActionType::kEffectRun:
            cmd.effect = ac.effect;
            cmd.effect_speed_ms = ac.speed_ms;
            cmd.fields = 0;
            if (ac.brightness >= 0) {
                cmd.state.brightness = static_cast<uint8_t>(ac.brightness);
                cmd.fields = kFieldBrightness;
            }
            ok = host_.light_command(cmd);
            break;
        case ActionType::kEffectStop:
            cmd.effect = "stop";
            cmd.fields = 0;
            ok = host_.light_command(cmd);
            break;
        case ActionType::kSceneActivate:
            ok = host_.scene_activate(ac.target, origin);
            break;
        case ActionType::kGroupControl:
        case ActionType::kRoomControl:
            cmd.fields = 0;
            if (ac.has_power) {
                cmd.state.power = ac.power;
                cmd.fields |= kFieldPower;
            }
            if (ac.brightness >= 0) {
                cmd.state.brightness = static_cast<uint8_t>(ac.brightness);
                cmd.fields |= kFieldBrightness;
            }
            ok = host_.group_command(ac.target, ac.type == ActionType::kRoomControl, cmd);
            break;
        case ActionType::kRelayToggle:
            if (host_.exists("light", ac.target)) {
                LightState s{};
                host_.light_state(ac.target, s);
                cmd.state.power = !s.power;
                cmd.fields = kFieldPower;
                ok = host_.light_command(cmd);
            } else {
                ok = host_.output_toggle(ac.target);
            }
            break;
        case ActionType::kDelay:
            run.resume_at = now_ms + ac.delay_ms;
            break;
        case ActionType::kMqttPublish:
            ok = host_.mqtt_publish(ac.topic, ac.payload, ac.retain);
            if (!ok) err = "MQTT is not connected";
            break;
        case ActionType::kUdpSend:
            ok = host_.udp_send(ac.topic, ac.port, ac.payload);
            break;
        case ActionType::kTcpSend:
            ok = host_.tcp_send(ac.topic, ac.port, ac.payload);
            break;
        case ActionType::kExecuteAutomation: {
            const std::size_t idx = index_of(ac.target);
            if (idx >= automations_.size()) {
                ok = false;
                err = "automation '" + ac.target + "' no longer exists";
            } else if (run.depth >= kMaxChainDepth) {
                finish(run, now_ms, "loop", "Nested automation depth limit reached");
                return;
            } else {
                fire(idx, "run by " + a->id, run.depth, now_ms, clock, false);
            }
            break;
        }
        case ActionType::kSetVariable: {
            if (variables_.size() >= kMaxVariables && variables_.find(ac.target) == variables_.end()) {
                ok = false;
                err = "variable limit reached";
                break;
            }
            const double old = variables_[ac.target];
            const double now = ac.value_op == "add" ? old + ac.value : ac.value;
            variables_[ac.target] = now;
            // Sensor-threshold triggers on variables (crossing semantics).
            for (std::size_t i = 0; i < automations_.size(); ++i) {
                const Automation &o = automations_[i];
                if (o.enabled && o.id != a->id && o.trigger.type == TriggerType::kSensorThreshold &&
                    o.trigger.target == ac.target && compare(now, o.trigger.op, o.trigger.threshold) &&
                    !compare(old, o.trigger.op, o.trigger.threshold)) {
                    if (run.depth >= kMaxChainDepth) continue;
                    fire(i, "variable " + ac.target, run.depth, now_ms, clock, true);
                }
            }
            break;
        }
        }
        if (!ok) {
            finish(run, now_ms, "error",
                   std::string(action_type_name(ac.type)) + " failed" + (err.empty() ? "" : ": " + err));
            return;
        }
    }
    if (run.active && run.pc >= a->actions.size()) {
        finish(run, now_ms, "ok", "");
    }
}

// ---------------------------------------------------------------------------
// Tick
// ---------------------------------------------------------------------------

void AutomationEngine::tick(uint32_t now_ms, const AutoClock &clock)
{
    if (!started_) {
        started_ = true;
        AutoEvent e;
        e.type = AutoEventType::kStartup;
        post(e);
    }
    // 1. Events (bounded per tick).
    std::size_t n = ev_count_;
    while (n-- > 0 && ev_count_ > 0) {
        const AutoEvent e = events_[ev_head_];
        ev_head_ = (ev_head_ + 1) % kMaxEvents;
        --ev_count_;
        dispatch(e, now_ms, clock);
    }
    // 2. Button gestures (long press, single-vs-double decisions).
    for (auto &kv : buttons_) {
        Button &b = kv.second;
        if (b.down && !b.long_fired && now_ms - b.down_at >= kLongPressMs) {
            b.long_fired = true;
            b.presses = 0;
            b.decide_at = 0;
            button_fire(kv.first, TriggerType::kLongPress, now_ms, clock);
        }
        if (b.decide_at != 0 && static_cast<int32_t>(now_ms - b.decide_at) >= 0 && !b.down) {
            b.decide_at = 0;
            const uint8_t presses = b.presses;
            b.presses = 0;
            button_fire(kv.first, presses >= 2 ? TriggerType::kDoublePress : TriggerType::kButtonPress, now_ms, clock);
        }
    }
    // 3. Time-based triggers.
    for (std::size_t i = 0; i < automations_.size(); ++i) {
        const Automation &a = automations_[i];
        if (!a.enabled) continue;
        const Trigger &t = a.trigger;
        Rate &rate = rate_[i];
        if (t.type == TriggerType::kTimerExpired) {
            if (rate.next_timer_ms == 0) {
                rate.next_timer_ms = now_ms + t.interval_s * 1000U;
            } else if (static_cast<int32_t>(now_ms - rate.next_timer_ms) >= 0) {
                rate.next_timer_ms = now_ms + t.interval_s * 1000U;
                fire(i, "every " + std::to_string(t.interval_s) + " s", 0, now_ms, clock, true);
            }
            continue;
        }
        if (!clock.valid) continue;
        int target = -1;
        if (t.type == TriggerType::kTimeSchedule) target = t.minute_of_day;
        if (t.type == TriggerType::kSunrise && clock.sunrise_min >= 0) target = clock.sunrise_min + t.offset_min;
        if (t.type == TriggerType::kSunset && clock.sunset_min >= 0) target = clock.sunset_min + t.offset_min;
        if (target < 0) continue;
        target = ((target % 1440) + 1440) % 1440;
        if (clock.minute_of_day != target || (t.days & (1U << clock.weekday)) == 0) continue;
        const int key = clock.day_id * 1440 + target;
        if (rate.last_schedule_key == key) continue; // once per matching minute
        rate.last_schedule_key = key;
        fire(i, t.type == TriggerType::kTimeSchedule ? "schedule " + format_hhmm(t.minute_of_day)
                                                     : std::string(trigger_type_name(t.type)),
             0, now_ms, clock, true);
    }
    // 4. Resume runs waiting on a delay.
    for (auto &r : runs_) {
        if (r.active) step_run(r, now_ms, clock);
    }
}

bool AutomationEngine::run_now(const std::string &id, uint32_t now_ms, const AutoClock &clock,
                               std::string &error)
{
    const std::size_t i = index_of(id);
    if (i >= automations_.size()) {
        error = "automation not found";
        return false;
    }
    const std::size_t before = hist_count_;
    fire(i, "manual test", 0, now_ms, clock, false);
    bool started = false;
    for (const auto &r : runs_) started |= r.active && r.id == id;
    if (!started && hist_count_ > before) {
        const HistoryEntry &last = history_[(hist_next_ + kHistory - 1) % kHistory];
        if (last.result != "ok") {
            error = last.message;
            return false;
        }
    }
    return true;
}

void AutomationEngine::cancel(const std::string &id, uint32_t now_ms)
{
    for (auto &r : runs_) {
        if (r.active && r.id == id) finish(r, now_ms, "cancelled", "Cancelled by user");
    }
}

bool AutomationEngine::set_variable(const std::string &name, double value)
{
    if (name.empty() || name.size() > 24) return false;
    if (variables_.size() >= kMaxVariables && variables_.find(name) == variables_.end()) return false;
    variables_[name] = value;
    return true;
}

void AutomationEngine::record(const HistoryEntry &h)
{
    history_[hist_next_] = h;
    hist_next_ = (hist_next_ + 1) % kHistory;
    if (hist_count_ < kHistory) ++hist_count_;
    if (listener_ != nullptr) listener_(h, listener_user_);
}

std::vector<HistoryEntry> AutomationEngine::history(const std::string &id) const
{
    std::vector<HistoryEntry> out;
    for (std::size_t k = 0; k < hist_count_; ++k) {
        const HistoryEntry &h = history_[(hist_next_ + kHistory - 1 - k) % kHistory]; // newest first
        if (id.empty() || h.automation_id == id) out.push_back(h);
    }
    return out;
}

// ---------------------------------------------------------------------------
// Sunrise / sunset (NOAA general solar position algorithm)
// ---------------------------------------------------------------------------

bool sun_times(int year, int month, int day, double latitude, double longitude, int utc_offset_min,
               int &sunrise_min, int &sunset_min)
{
    constexpr double kPi = 3.14159265358979323846;
    const double rad = kPi / 180.0;
    // Day of year.
    static const int cum[] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};
    const bool leap = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
    const int n = cum[month - 1] + day + ((leap && month > 2) ? 1 : 0);
    const double gamma = 2.0 * kPi / (leap ? 366.0 : 365.0) * (n - 1);
    const double eqtime = 229.18 * (0.000075 + 0.001868 * std::cos(gamma) - 0.032077 * std::sin(gamma) -
                                    0.014615 * std::cos(2 * gamma) - 0.040849 * std::sin(2 * gamma));
    const double decl = 0.006918 - 0.399912 * std::cos(gamma) + 0.070257 * std::sin(gamma) -
                        0.006758 * std::cos(2 * gamma) + 0.000907 * std::sin(2 * gamma) -
                        0.002697 * std::cos(3 * gamma) + 0.00148 * std::sin(3 * gamma);
    const double cos_ha = std::cos(90.833 * rad) / (std::cos(latitude * rad) * std::cos(decl)) -
                          std::tan(latitude * rad) * std::tan(decl);
    if (cos_ha < -1.0 || cos_ha > 1.0) return false; // polar day/night
    const double ha = std::acos(cos_ha) / rad;
    const double rise_utc = 720.0 - 4.0 * (longitude + ha) - eqtime;
    const double set_utc = 720.0 - 4.0 * (longitude - ha) - eqtime;
    auto wrap = [](double m) { return static_cast<int>(std::lround(std::fmod(std::fmod(m, 1440.0) + 1440.0, 1440.0))) % 1440; };
    sunrise_min = wrap(rise_utc + utc_offset_min);
    sunset_min = wrap(set_utc + utc_offset_min);
    return true;
}

} // namespace lumen
