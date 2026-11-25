// Host tests for the automation engine (SPEC §11): parsing/validation,
// triggers, conditions, actions, delays, button gestures, schedules,
// loop/recursion prevention, rate limiting, history.
#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include <unity.h>

#include "core/cJSON.h"
#include "lumen/automation/automation.h"
#include "lumen/automation/automation_engine.h"

using namespace lumen;

namespace
{

class FakeHost final : public IAutomationHost
{
  public:
    std::map<std::string, LightState> lights;
    std::map<std::string, bool> inputs;
    std::vector<LightCommand> commands;
    std::vector<std::string> scenes;
    std::vector<std::string> published;
    bool mqtt_up = true;

    bool light_state(const std::string &id, LightState &out) override
    {
        auto it = lights.find(id);
        if (it == lights.end()) return false;
        out = it->second;
        return true;
    }
    bool input_state(const std::string &id, bool &out) override
    {
        auto it = inputs.find(id);
        if (it == inputs.end()) return false;
        out = it->second;
        return true;
    }
    bool exists(const char *kind, const std::string &id) override
    {
        const std::string k = kind;
        if (k == "light") return lights.count(id) != 0;
        if (k == "input") return inputs.count(id) != 0;
        if (k == "scene") return id == "night";
        if (k == "group" || k == "room") return id == "downstairs";
        return false;
    }
    bool light_command(const LightCommand &cmd) override
    {
        commands.push_back(cmd);
        LightState &s = lights[cmd.channel_id];
        if (cmd.fields & kFieldPower) s.power = cmd.state.power;
        if (cmd.fields & kFieldBrightness) {
            s.brightness = cmd.state.brightness;
            if (!(cmd.fields & kFieldPower)) s.power = cmd.state.brightness > 0;
        }
        return true;
    }
    bool scene_activate(const std::string &id, const std::string &) override
    {
        scenes.push_back(id);
        return true;
    }
    bool group_command(const std::string &, bool, const LightCommand &) override { return true; }
    bool output_toggle(const std::string &) override { return true; }
    bool mqtt_publish(const std::string &topic, const std::string &payload, bool) override
    {
        if (!mqtt_up) return false;
        published.push_back(topic + "=" + payload);
        return true;
    }
    bool udp_send(const std::string &, uint16_t, const std::string &) override { return true; }
    bool tcp_send(const std::string &, uint16_t, const std::string &) override { return true; }
};

Automation parse(const char *json)
{
    cJSON *o = cJSON_Parse(json);
    TEST_ASSERT_NOT_NULL_MESSAGE(o, "fixture JSON invalid");
    Automation a;
    std::string err;
    const bool ok = automation_from_json(o, a, err);
    cJSON_Delete(o);
    TEST_ASSERT_TRUE_MESSAGE(ok, err.c_str());
    return a;
}

bool parse_fails(const char *json, std::string &err)
{
    cJSON *o = cJSON_Parse(json);
    Automation a;
    const bool ok = automation_from_json(o, a, err);
    cJSON_Delete(o);
    return !ok;
}

AutoClock at(int hh, int mm, int weekday = 0)
{
    AutoClock c;
    c.valid = true;
    c.unix_time = 1800000000;
    c.minute_of_day = hh * 60 + mm;
    c.weekday = weekday;
    c.day_id = 100;
    c.sunrise_min = 6 * 60;
    c.sunset_min = 18 * 60 + 30;
    return c;
}

AutoEvent input(const char *id, bool state)
{
    AutoEvent e;
    e.type = AutoEventType::kInput;
    e.id = id;
    e.state = state;
    return e;
}

} // namespace

extern "C" void setUp(void) {}
extern "C" void tearDown(void) {}

static void test_spec_example_parses_and_round_trips(void)
{
    Automation a = parse(R"({"id":"kitchen_evening","name":"Kitchen Evening","enabled":true,
      "trigger":{"type":"input_state","input_id":"di1","state":true},
      "conditions":{"operator":"AND","items":[{"type":"time_range","start":"18:00","end":"23:00"}]},
      "actions":[{"type":"light_power","light_id":"kitchen","power":true},
                 {"type":"light_brightness","light_id":"kitchen","brightness":80,"transition_ms":500}],
      "execution":{"cooldown_ms":1000,"max_executions_per_hour":100,"allow_parallel":false}})");
    TEST_ASSERT_EQUAL(static_cast<int>(TriggerType::kInputState), static_cast<int>(a.trigger.type));
    TEST_ASSERT_EQUAL(2, static_cast<int>(a.actions.size()));
    TEST_ASSERT_EQUAL(static_cast<int>(ActionType::kLightOn), static_cast<int>(a.actions[0].type));
    cJSON *round = automation_to_json(a);
    Automation b;
    std::string err;
    TEST_ASSERT_TRUE_MESSAGE(automation_from_json(round, b, err), err.c_str());
    cJSON_Delete(round);
    TEST_ASSERT_EQUAL(1080, b.conditions.items[0].start_min);
    TEST_ASSERT_EQUAL(1000U, b.execution.cooldown_ms);
    TEST_ASSERT_NOT_EQUAL(std::string::npos, automation_summary(b).find("input di1 turns on"));
}

static void test_invalid_documents_rejected(void)
{
    std::string err;
    TEST_ASSERT_TRUE(parse_fails(R"({"id":"x","name":"x","trigger":{"type":"bogus"},"actions":[{"type":"light_on","light_id":"a"}]})", err));
    TEST_ASSERT_TRUE(parse_fails(R"({"id":"x","name":"x","trigger":{"type":"controller_startup"},"actions":[]})", err));
    TEST_ASSERT_TRUE(parse_fails(R"({"id":"x","name":"x","trigger":{"type":"time_schedule","time":"25:00"},"actions":[{"type":"light_on","light_id":"a"}]})", err));
    TEST_ASSERT_TRUE(parse_fails(R"({"id":"x","name":"x","trigger":{"type":"controller_startup"},"actions":[{"type":"light_brightness","light_id":"a","brightness":150}]})", err));
    TEST_ASSERT_TRUE(parse_fails(R"({"id":"Bad Id","name":"x","trigger":{"type":"controller_startup"},"actions":[{"type":"light_on","light_id":"a"}]})", err));
    TEST_ASSERT_TRUE(parse_fails(R"({"id":"x","name":"x","trigger":{"type":"controller_startup"},"actions":[{"type":"shell","cmd":"rm -rf /"}]})", err));
}

static void test_input_trigger_with_time_condition(void)
{
    FakeHost host;
    host.lights["kitchen"] = {};
    host.inputs["di1"] = false;
    AutomationEngine eng(host);
    eng.upsert(parse(R"({"id":"k","name":"k","trigger":{"type":"input_state","target":"di1","state":true},
      "conditions":{"operator":"AND","items":[{"type":"time_range","start":"18:00","end":"23:00"}]},
      "actions":[{"type":"light_on","target":"kitchen","brightness":80}]})"));
    eng.post(input("di1", true));
    eng.tick(1000, at(12, 0)); // outside the window
    TEST_ASSERT_EQUAL(0, static_cast<int>(host.commands.size()));
    eng.post(input("di1", false));
    eng.post(input("di1", true));
    eng.tick(2000, at(19, 0));
    TEST_ASSERT_EQUAL(1, static_cast<int>(host.commands.size()));
    TEST_ASSERT_TRUE(host.lights["kitchen"].power);
    TEST_ASSERT_EQUAL_UINT8(80, host.lights["kitchen"].brightness);
    TEST_ASSERT_EQUAL_STRING("auto:k:1", host.commands[0].origin.c_str());
    TEST_ASSERT_EQUAL_STRING("ok", eng.history("k")[0].result.c_str());
}

static void test_time_range_across_midnight_and_weekday(void)
{
    FakeHost host;
    host.lights["a"] = {};
    AutomationEngine eng(host);
    eng.upsert(parse(R"({"id":"n","name":"n","trigger":{"type":"timer_expired","interval_s":10},
      "conditions":{"operator":"AND","items":[{"type":"time_range","start":"22:00","end":"06:00"},
                                             {"type":"day_of_week","days":["sat","sun"]}]},
      "actions":[{"type":"light_toggle","target":"a"}]})"));
    eng.tick(0, at(23, 0, 5));      // arms the timer
    eng.tick(10000, at(23, 0, 5));  // Saturday 23:00 -> runs
    TEST_ASSERT_EQUAL(1, static_cast<int>(host.commands.size()));
    eng.tick(20000, at(23, 0, 2));  // Wednesday -> no
    TEST_ASSERT_EQUAL(1, static_cast<int>(host.commands.size()));
    eng.tick(30000, at(3, 0, 6));   // Sunday 03:00 -> runs (spans midnight)
    TEST_ASSERT_EQUAL(2, static_cast<int>(host.commands.size()));
}

static void test_delay_action_resumes_later_and_cancel(void)
{
    FakeHost host;
    host.lights["a"] = {};
    AutomationEngine eng(host);
    eng.upsert(parse(R"({"id":"d","name":"d","trigger":{"type":"input_state","target":"di1","state":true},
      "actions":[{"type":"light_on","target":"a"},{"type":"delay","delay_ms":5000},{"type":"light_off","target":"a"}]})"));
    host.inputs["di1"] = false;
    eng.post(input("di1", true));
    eng.tick(0, at(10, 0));
    TEST_ASSERT_TRUE(host.lights["a"].power);
    TEST_ASSERT_TRUE(eng.runtime("d")->running);
    eng.tick(4000, at(10, 0));
    TEST_ASSERT_TRUE(host.lights["a"].power);
    eng.tick(5001, at(10, 0));
    TEST_ASSERT_FALSE(host.lights["a"].power);
    TEST_ASSERT_FALSE(eng.runtime("d")->running);
    // Cancel mid-delay.
    eng.post(input("di1", false));
    eng.post(input("di1", true));
    eng.tick(20000, at(10, 0));
    eng.cancel("d", 21000);
    eng.tick(30000, at(10, 0));
    TEST_ASSERT_TRUE(host.lights["a"].power); // the "off" never ran
    TEST_ASSERT_EQUAL_STRING("cancelled", eng.history("d")[0].result.c_str());
}

static void test_button_gestures(void)
{
    FakeHost host;
    host.lights["a"] = {};
    host.lights["b"] = {};
    host.lights["c"] = {};
    host.inputs["btn"] = false;
    AutomationEngine eng(host);
    eng.upsert(parse(R"({"id":"p","name":"p","trigger":{"type":"button_press","target":"btn"},"actions":[{"type":"light_toggle","target":"a"}]})"));
    eng.upsert(parse(R"({"id":"dp","name":"dp","trigger":{"type":"double_press","target":"btn"},"actions":[{"type":"light_toggle","target":"b"}]})"));
    eng.upsert(parse(R"({"id":"lp","name":"lp","trigger":{"type":"long_press","target":"btn"},"actions":[{"type":"light_toggle","target":"c"}]})"));
    const AutoClock c = at(10, 0);
    // Single press: decided after the double-press window.
    eng.post(input("btn", true));
    eng.tick(0, c);
    eng.post(input("btn", false));
    eng.tick(100, c);
    TEST_ASSERT_FALSE(host.lights["a"].power);
    eng.tick(600, c);
    TEST_ASSERT_TRUE(host.lights["a"].power);
    // Double press.
    eng.post(input("btn", true));
    eng.tick(1000, c);
    eng.post(input("btn", false));
    eng.tick(1100, c);
    eng.post(input("btn", true));
    eng.tick(1200, c);
    eng.post(input("btn", false));
    eng.tick(1300, c);
    TEST_ASSERT_TRUE(host.lights["b"].power);
    TEST_ASSERT_TRUE(host.lights["a"].power); // no extra single press
    // Long press.
    eng.post(input("btn", true));
    eng.tick(3000, c);
    eng.tick(3900, c);
    TEST_ASSERT_TRUE(host.lights["c"].power);
    eng.post(input("btn", false));
    eng.tick(4000, c);
    eng.tick(5000, c);
    TEST_ASSERT_TRUE(host.lights["a"].power); // release after long press is not a press
}

static void test_schedule_fires_once_per_minute_and_sunset_offset(void)
{
    FakeHost host;
    host.lights["a"] = {};
    AutomationEngine eng(host);
    eng.upsert(parse(R"({"id":"s","name":"s","trigger":{"type":"time_schedule","time":"07:30","days":["mon"]},"actions":[{"type":"light_toggle","target":"a"}]})"));
    eng.upsert(parse(R"({"id":"ss","name":"ss","trigger":{"type":"sunset","offset_min":-30},"actions":[{"type":"scene_activate","target":"night"}]})"));
    eng.tick(0, at(7, 30, 0));
    eng.tick(1000, at(7, 30, 0)); // same minute: not again
    TEST_ASSERT_EQUAL(1, static_cast<int>(host.commands.size()));
    eng.tick(2000, at(7, 30, 1)); // Tuesday: not scheduled
    TEST_ASSERT_EQUAL(1, static_cast<int>(host.commands.size()));
    eng.tick(3000, at(18, 0, 1)); // sunset 18:30 - 30 min
    TEST_ASSERT_EQUAL(1, static_cast<int>(host.scenes.size()));
    AutoClock unsynced = at(18, 0, 1);
    unsynced.valid = false;
    unsynced.day_id = 101;
    eng.tick(4000, unsynced); // unknown time never fires schedules
    TEST_ASSERT_EQUAL(1, static_cast<int>(host.scenes.size()));
}

static void test_self_trigger_and_loop_prevention(void)
{
    FakeHost host;
    host.lights["a"] = {};
    host.lights["b"] = {};
    AutomationEngine eng(host);
    // A: a changes -> toggle b.  B: b changes -> toggle a.  (ping-pong)
    eng.upsert(parse(R"({"id":"pa","name":"pa","trigger":{"type":"light_state","target":"a"},"actions":[{"type":"light_toggle","target":"b"}]})"));
    eng.upsert(parse(R"({"id":"pb","name":"pb","trigger":{"type":"light_state","target":"b"},"actions":[{"type":"light_toggle","target":"a"}]})"));
    AutoEvent e;
    e.type = AutoEventType::kLight;
    e.id = "a";
    e.light.power = true;
    e.origin = "rest";
    eng.post(e);
    // Feed back every light command as a light event with its origin.
    int last_active_tick = -1;
    for (uint32_t t = 0; t < 40; ++t) {
        eng.tick(t * 100, at(10, 0));
        if (!host.commands.empty()) last_active_tick = static_cast<int>(t);
        std::vector<LightCommand> cmds = host.commands;
        host.commands.clear();
        for (const auto &c : cmds) {
            AutoEvent fe;
            fe.type = AutoEventType::kLight;
            fe.id = c.channel_id;
            fe.light = host.lights[c.channel_id];
            fe.prev = fe.light;
            fe.prev.power = !fe.light.power;
            fe.origin = c.origin;
            eng.post(fe);
        }
    }
    bool loop_recorded = false;
    for (const auto &h : eng.history()) loop_recorded |= h.result == "loop";
    TEST_ASSERT_TRUE(loop_recorded);
    TEST_ASSERT_TRUE_MESSAGE(last_active_tick < 15, "automations kept triggering each other");
    char msg[64];
    std::snprintf(msg, sizeof(msg), "pa runs=%u pb runs=%u", static_cast<unsigned>(eng.runtime("pa")->runs),
                  static_cast<unsigned>(eng.runtime("pb")->runs));
    // Each independent chain is cut at kMaxChainDepth; two chains start here
    // (rest -> pa, and self's own command), so runs stay small and bounded.
    TEST_ASSERT_TRUE_MESSAGE(eng.runtime("pa")->runs <= 2U * AutomationEngine::kMaxChainDepth, msg);
}

static void test_automation_never_retriggers_itself(void)
{
    FakeHost host;
    host.lights["a"] = {};
    AutomationEngine eng(host);
    eng.upsert(parse(R"({"id":"self","name":"self","trigger":{"type":"light_state","target":"a"},"actions":[{"type":"light_toggle","target":"a"}]})"));
    AutoEvent e;
    e.type = AutoEventType::kLight;
    e.id = "a";
    e.light.power = true;
    e.origin = "rest";
    eng.post(e);
    for (uint32_t t = 0; t < 10; ++t) {
        eng.tick(t * 100, at(10, 0));
        for (const auto &c : host.commands) {
            AutoEvent fe;
            fe.type = AutoEventType::kLight;
            fe.id = c.channel_id;
            fe.light = host.lights[c.channel_id];
            fe.prev.power = !fe.light.power;
            fe.origin = c.origin;
            eng.post(fe);
        }
        host.commands.clear();
    }
    TEST_ASSERT_EQUAL(1U, eng.runtime("self")->runs);
}

static void test_execute_automation_cycle_rejected_by_validation(void)
{
    FakeHost host;
    host.lights["a"] = {};
    AutomationEngine eng(host);
    eng.upsert(parse(R"({"id":"x","name":"x","trigger":{"type":"controller_startup"},"actions":[{"type":"execute_automation","target":"y"}]})"));
    Automation y = parse(R"({"id":"y","name":"y","trigger":{"type":"controller_startup"},"actions":[{"type":"execute_automation","target":"x"}]})");
    std::string err;
    TEST_ASSERT_FALSE(eng.validate(y, err));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, err.find("loop"));
    Automation selfref = parse(R"({"id":"z","name":"z","trigger":{"type":"controller_startup"},"actions":[{"type":"execute_automation","target":"z"}]})");
    TEST_ASSERT_FALSE(eng.validate(selfref, err));
    Automation missing = parse(R"({"id":"m","name":"m","trigger":{"type":"controller_startup"},"actions":[{"type":"light_on","target":"ghost"}]})");
    TEST_ASSERT_FALSE(eng.validate(missing, err));
}

static void test_cooldown_rate_limit_and_parallel(void)
{
    FakeHost host;
    host.lights["a"] = {};
    host.inputs["di1"] = false;
    AutomationEngine eng(host);
    eng.upsert(parse(R"({"id":"r","name":"r","trigger":{"type":"input_state","target":"di1","state":true},
      "actions":[{"type":"light_toggle","target":"a"}],
      "execution":{"cooldown_ms":1000,"max_executions_per_hour":3}})"));
    uint32_t t = 0;
    for (int i = 0; i < 10; ++i) {
        eng.post(input("di1", false));
        eng.post(input("di1", true));
        eng.tick(t, at(10, 0));
        t += 300; // faster than the cooldown
    }
    TEST_ASSERT_TRUE(eng.runtime("r")->runs <= 3);
    for (int i = 0; i < 10; ++i) {
        t += 2000;
        eng.post(input("di1", false));
        eng.post(input("di1", true));
        eng.tick(t, at(10, 0));
    }
    TEST_ASSERT_EQUAL(3U, eng.runtime("r")->runs); // hourly cap
    TEST_ASSERT_EQUAL_STRING("rate_limited", eng.history("r")[0].result.c_str());
    // Single-run mode: a second trigger during a delay is skipped.
    eng.upsert(parse(R"({"id":"slow","name":"slow","trigger":{"type":"input_state","target":"di2","state":true},
      "actions":[{"type":"delay","delay_ms":10000},{"type":"light_toggle","target":"a"}]})"));
    host.inputs["di2"] = false;
    eng.post(input("di2", true));
    eng.tick(100000, at(10, 0));
    eng.post(input("di2", false));
    eng.post(input("di2", true));
    eng.tick(101000, at(10, 0));
    TEST_ASSERT_EQUAL_STRING("skipped", eng.history("slow")[0].result.c_str());
}

static void test_variables_and_sensor_threshold_and_failures(void)
{
    FakeHost host;
    host.lights["a"] = {};
    AutomationEngine eng(host);
    eng.upsert(parse(R"({"id":"count","name":"count","trigger":{"type":"timer_expired","interval_s":10},"actions":[{"type":"set_variable","target":"n","value":1,"op":"add"}]})"));
    eng.upsert(parse(R"({"id":"alarm","name":"alarm","trigger":{"type":"sensor_threshold","target":"n","threshold":2,"operator":"ge"},"actions":[{"type":"mqtt_publish","topic":"lumen/alarm","payload":"n>=2"}]})"));
    eng.tick(0, at(10, 0));
    eng.tick(10000, at(10, 0));
    TEST_ASSERT_EQUAL(0, static_cast<int>(host.published.size()));
    eng.tick(20000, at(10, 0));
    TEST_ASSERT_EQUAL(1, static_cast<int>(host.published.size()));
    eng.tick(30000, at(10, 0)); // stays above: no re-fire (crossing semantics)
    TEST_ASSERT_EQUAL(1, static_cast<int>(host.published.size()));
    TEST_ASSERT_EQUAL(3, static_cast<int>(eng.variables().at("n")));
    // A failing action is recorded, the engine keeps running.
    host.mqtt_up = false;
    std::string err;
    eng.run_now("alarm", 40000, at(10, 0), err);
    TEST_ASSERT_EQUAL_STRING("error", eng.history("alarm")[0].result.c_str());
    eng.tick(50000, at(10, 0));
    TEST_ASSERT_EQUAL(4, static_cast<int>(eng.variables().at("n"))); // missed periods do not burst
}

static void test_sun_times_reference(void)
{
    // Colombo (6.93 N, 79.85 E, UTC+5:30) on 2026-06-21: sunrise ~05:53, sunset ~18:26.
    int rise = 0, set = 0;
    TEST_ASSERT_TRUE(sun_times(2026, 6, 21, 6.93, 79.85, 330, rise, set));
    TEST_ASSERT_INT_WITHIN(4, 5 * 60 + 53, rise);
    TEST_ASSERT_INT_WITHIN(4, 18 * 60 + 26, set);
    // Polar night: no sunrise.
    TEST_ASSERT_FALSE(sun_times(2026, 12, 21, 80.0, 0.0, 0, rise, set));
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_spec_example_parses_and_round_trips);
    RUN_TEST(test_invalid_documents_rejected);
    RUN_TEST(test_input_trigger_with_time_condition);
    RUN_TEST(test_time_range_across_midnight_and_weekday);
    RUN_TEST(test_delay_action_resumes_later_and_cancel);
    RUN_TEST(test_button_gestures);
    RUN_TEST(test_schedule_fires_once_per_minute_and_sunset_offset);
    RUN_TEST(test_self_trigger_and_loop_prevention);
    RUN_TEST(test_automation_never_retriggers_itself);
    RUN_TEST(test_execute_automation_cycle_rejected_by_validation);
    RUN_TEST(test_cooldown_rate_limit_and_parallel);
    RUN_TEST(test_variables_and_sensor_threshold_and_failures);
    RUN_TEST(test_sun_times_reference);
    return UNITY_END();
}
