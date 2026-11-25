// Lumen — automation model (SPEC §11, schemas/automation.schema.json).
//
// Trigger -> conditions (nested AND/OR/NOT tree) -> sequential actions.
// Parsed from the per-automation JSON document into a compact in-RAM form;
// the advanced editor's graph layout stays in the file only ("graph").
// Nothing here executes user code: every action is a fixed, typed operation
// (AGENTS.md §7).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/cJSON.h"

namespace lumen
{

enum class TriggerType : uint8_t
{
    kLightState = 0,   // light power changes (optionally to `state`)
    kBrightnessChange, // light brightness crosses/changes (optional threshold)
    kColorChange,
    kInputState,       // input edge (optionally to `state`)
    kButtonPress,
    kDoublePress,
    kLongPress,
    kTimeSchedule,     // HH:MM on selected days
    kSunrise,          // +/- offset minutes
    kSunset,
    kSensorThreshold,  // variable crosses threshold
    kMqttEvent,        // topic (+ optional payload match)
    kEspNowEvent,      // peer event name
    kSceneActivated,
    kGroupStateChanged,
    kControllerStartup,
    kTimerExpired,     // periodic interval
};

enum class CompareOp : uint8_t
{
    kEq = 0,
    kNe,
    kGt,
    kLt,
    kGe,
    kLe,
};

struct Trigger
{
    TriggerType type = TriggerType::kControllerStartup;
    std::string target;      // light/input/scene/group id, MQTT topic, peer event
    bool has_state = false;  // match only transitions to `state`
    bool state = false;
    bool has_threshold = false;
    CompareOp op = CompareOp::kGt; // brightness/sensor threshold comparison
    double threshold = 0;
    std::string payload;     // MQTT payload match ("" = any)
    int16_t minute_of_day = -1; // time_schedule
    uint8_t days = 0x7F;     // bit0 = Monday .. bit6 = Sunday
    int16_t offset_min = 0;  // sunrise/sunset offset
    uint32_t interval_s = 0; // timer_expired period
};

enum class ConditionType : uint8_t
{
    kAnd = 0,
    kOr,
    kNot,
    kTimeRange,
    kDayOfWeek,
    kCompare, // light/input/variable state comparison
};

struct Condition
{
    ConditionType type = ConditionType::kAnd;
    std::vector<Condition> items; // AND/OR/NOT children
    // time range (minutes of day; end < start spans midnight)
    int16_t start_min = 0;
    int16_t end_min = 0;
    uint8_t days = 0x7F;
    // comparison: source "light" | "input" | "variable"
    std::string source;
    std::string id;
    std::string field; // power | brightness | red | green | blue | color_temperature
    CompareOp op = CompareOp::kEq;
    double value = 0;
};

enum class ActionType : uint8_t
{
    kLightOn = 0,
    kLightOff,
    kLightToggle,
    kLightBrightness,
    kLightColor,
    kLightCct,
    kSceneActivate,
    kGroupControl,
    kRoomControl,
    kRelayToggle,
    kEffectRun,
    kEffectStop,
    kDelay,
    kMqttPublish,
    kUdpSend,
    kTcpSend,
    kExecuteAutomation,
    kSetVariable,
};

struct Action
{
    ActionType type = ActionType::kLightOn;
    std::string target;   // light/scene/group/room/automation id, variable name
    bool power = true;    // group/room control
    bool has_power = false;
    int16_t brightness = -1;
    uint8_t red = 0, green = 0, blue = 0;
    uint16_t kelvin = 0;
    uint32_t transition_ms = 0;
    bool has_transition = false;
    std::string effect;
    uint32_t speed_ms = 0;
    uint32_t delay_ms = 0;
    std::string topic;    // MQTT topic / host for UDP/TCP
    std::string payload;
    uint16_t port = 0;
    bool retain = false;
    double value = 0;     // set_variable value
    std::string value_op; // "set" | "add"
};

struct ExecutionSettings
{
    uint32_t cooldown_ms = 0;
    uint16_t max_per_hour = 60;
    bool allow_parallel = false;
};

struct Automation
{
    std::string id;
    std::string name;
    bool enabled = true;
    Trigger trigger;
    bool has_conditions = false;
    Condition conditions; // root (AND/OR/NOT)
    std::vector<Action> actions;
    ExecutionSettings execution;
    std::string mode = "simple"; // editor that last saved it: simple | advanced
};

constexpr std::size_t kMaxAutomations = 32;
constexpr std::size_t kMaxActions = 32;
constexpr std::size_t kMaxConditionDepth = 4;
constexpr std::size_t kMaxConditionItems = 16;

const char *trigger_type_name(TriggerType t);
const char *action_type_name(ActionType t);

// Parse an automation document. `error` gets a human-readable reason (the
// JSON pointer-ish path of the offending field is included).
bool automation_from_json(const cJSON *obj, Automation &out, std::string &error);

// Serialise to the canonical document (without the editor graph).
cJSON *automation_to_json(const Automation &a);

// "HH:MM" <-> minutes of day.
int16_t parse_hhmm(const char *s);
std::string format_hhmm(int16_t minutes);

// One-line plain-language summary for the UI ("When DI1 turns on, ...").
std::string automation_summary(const Automation &a);

} // namespace lumen
