// Lumen — automation model: JSON parse/serialise/summary. See automation.h
#include "lumen/automation/automation.h"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace lumen
{

namespace
{

struct Name
{
    uint8_t v;
    const char *name;
};

constexpr Name kTriggers[] = {
    {static_cast<uint8_t>(TriggerType::kLightState), "light_state"},
    {static_cast<uint8_t>(TriggerType::kBrightnessChange), "brightness_change"},
    {static_cast<uint8_t>(TriggerType::kColorChange), "color_change"},
    {static_cast<uint8_t>(TriggerType::kInputState), "input_state"},
    {static_cast<uint8_t>(TriggerType::kButtonPress), "button_press"},
    {static_cast<uint8_t>(TriggerType::kDoublePress), "double_press"},
    {static_cast<uint8_t>(TriggerType::kLongPress), "long_press"},
    {static_cast<uint8_t>(TriggerType::kTimeSchedule), "time_schedule"},
    {static_cast<uint8_t>(TriggerType::kSunrise), "sunrise"},
    {static_cast<uint8_t>(TriggerType::kSunset), "sunset"},
    {static_cast<uint8_t>(TriggerType::kSensorThreshold), "sensor_threshold"},
    {static_cast<uint8_t>(TriggerType::kMqttEvent), "mqtt_event"},
    {static_cast<uint8_t>(TriggerType::kEspNowEvent), "esp_now_event"},
    {static_cast<uint8_t>(TriggerType::kSceneActivated), "scene_activated"},
    {static_cast<uint8_t>(TriggerType::kGroupStateChanged), "group_state_changed"},
    {static_cast<uint8_t>(TriggerType::kControllerStartup), "controller_startup"},
    {static_cast<uint8_t>(TriggerType::kTimerExpired), "timer_expired"},
};

constexpr Name kActions[] = {
    {static_cast<uint8_t>(ActionType::kLightOn), "light_on"},
    {static_cast<uint8_t>(ActionType::kLightOff), "light_off"},
    {static_cast<uint8_t>(ActionType::kLightToggle), "light_toggle"},
    {static_cast<uint8_t>(ActionType::kLightBrightness), "light_brightness"},
    {static_cast<uint8_t>(ActionType::kLightColor), "light_color"},
    {static_cast<uint8_t>(ActionType::kLightCct), "light_cct"},
    {static_cast<uint8_t>(ActionType::kSceneActivate), "scene_activate"},
    {static_cast<uint8_t>(ActionType::kGroupControl), "group_control"},
    {static_cast<uint8_t>(ActionType::kRoomControl), "room_control"},
    {static_cast<uint8_t>(ActionType::kRelayToggle), "relay_toggle"},
    {static_cast<uint8_t>(ActionType::kEffectRun), "effect_run"},
    {static_cast<uint8_t>(ActionType::kEffectStop), "effect_stop"},
    {static_cast<uint8_t>(ActionType::kDelay), "delay"},
    {static_cast<uint8_t>(ActionType::kMqttPublish), "mqtt_publish"},
    {static_cast<uint8_t>(ActionType::kUdpSend), "udp_send"},
    {static_cast<uint8_t>(ActionType::kTcpSend), "tcp_send"},
    {static_cast<uint8_t>(ActionType::kExecuteAutomation), "execute_automation"},
    {static_cast<uint8_t>(ActionType::kSetVariable), "set_variable"},
};

// Number formatting without std::to_string(double): libstdc++'s floating
// to_chars drags ~110 KB of Ryu tables into the firmware image.
std::string fmt_num(double v)
{
    char buf[24];
    std::snprintf(buf, sizeof(buf), "%g", v);
    return buf;
}

const char *kDays[] = {"mon", "tue", "wed", "thu", "fri", "sat", "sun"};

std::string str(const cJSON *o, const char *k)
{
    const cJSON *it = cJSON_GetObjectItemCaseSensitive(o, k);
    return cJSON_IsString(it) && it->valuestring ? std::string(it->valuestring) : std::string();
}

bool num(const cJSON *o, const char *k, double &out)
{
    const cJSON *it = cJSON_GetObjectItemCaseSensitive(o, k);
    if (!cJSON_IsNumber(it)) return false;
    out = it->valuedouble;
    return true;
}

bool boolean(const cJSON *o, const char *k, bool &out)
{
    const cJSON *it = cJSON_GetObjectItemCaseSensitive(o, k);
    if (!cJSON_IsBool(it)) return false;
    out = cJSON_IsTrue(it);
    return true;
}

// First non-empty of several alias keys.
std::string first_str(const cJSON *o, std::initializer_list<const char *> keys)
{
    for (const char *k : keys) {
        std::string v = str(o, k);
        if (!v.empty()) return v;
    }
    return std::string();
}

bool parse_days(const cJSON *o, uint8_t &mask, std::string &error)
{
    const cJSON *arr = cJSON_GetObjectItemCaseSensitive(o, "days");
    if (arr == nullptr) return true; // keep default (all days)
    if (!cJSON_IsArray(arr)) {
        error = "days must be an array like [\"mon\",\"fri\"]";
        return false;
    }
    mask = 0;
    const cJSON *d = nullptr;
    cJSON_ArrayForEach(d, arr)
    {
        bool ok = false;
        for (int i = 0; i < 7 && cJSON_IsString(d); ++i) {
            if (std::strcmp(d->valuestring, kDays[i]) == 0) {
                mask |= static_cast<uint8_t>(1U << i);
                ok = true;
            }
        }
        if (!ok) {
            error = "unknown day in days[]";
            return false;
        }
    }
    if (mask == 0) {
        error = "days must contain at least one day";
        return false;
    }
    return true;
}

cJSON *days_json(uint8_t mask)
{
    cJSON *arr = cJSON_CreateArray();
    for (int i = 0; i < 7; ++i) {
        if (mask & (1U << i)) cJSON_AddItemToArray(arr, cJSON_CreateString(kDays[i]));
    }
    return arr;
}

bool parse_op(const std::string &s, CompareOp &op)
{
    static const struct
    {
        const char *a;
        const char *b;
        CompareOp op;
    } map[] = {{"eq", "==", CompareOp::kEq},      {"ne", "!=", CompareOp::kNe},
               {"gt", ">", CompareOp::kGt},       {"lt", "<", CompareOp::kLt},
               {"ge", ">=", CompareOp::kGe},      {"le", "<=", CompareOp::kLe},
               {"equal", "", CompareOp::kEq},     {"not_equal", "", CompareOp::kNe},
               {"greater_than", "", CompareOp::kGt}, {"less_than", "", CompareOp::kLt},
               {"greater_than_equal", "", CompareOp::kGe}, {"less_than_equal", "", CompareOp::kLe}};
    for (const auto &m : map) {
        if (s == m.a || (m.b[0] && s == m.b)) {
            op = m.op;
            return true;
        }
    }
    return false;
}

const char *op_name(CompareOp op)
{
    switch (op) {
    case CompareOp::kEq: return "eq";
    case CompareOp::kNe: return "ne";
    case CompareOp::kGt: return "gt";
    case CompareOp::kLt: return "lt";
    case CompareOp::kGe: return "ge";
    case CompareOp::kLe: return "le";
    }
    return "eq";
}

const char *op_symbol(CompareOp op)
{
    switch (op) {
    case CompareOp::kEq: return "=";
    case CompareOp::kNe: return "≠";
    case CompareOp::kGt: return ">";
    case CompareOp::kLt: return "<";
    case CompareOp::kGe: return "≥";
    case CompareOp::kLe: return "≤";
    }
    return "=";
}

bool parse_condition(const cJSON *o, Condition &c, std::size_t depth, std::string &error)
{
    if (depth > kMaxConditionDepth) {
        error = "conditions nested too deeply";
        return false;
    }
    std::string type = str(o, "type");
    if (type.empty()) type = str(o, "operator"); // root / group form
    for (auto &ch : type) ch = static_cast<char>(std::tolower(ch));
    if (type == "and" || type == "or" || type == "not") {
        c.type = type == "and" ? ConditionType::kAnd : type == "or" ? ConditionType::kOr : ConditionType::kNot;
        const cJSON *items = cJSON_GetObjectItemCaseSensitive(o, "items");
        const cJSON *it = nullptr;
        cJSON_ArrayForEach(it, items)
        {
            if (c.items.size() >= kMaxConditionItems) {
                error = "too many conditions";
                return false;
            }
            Condition child;
            if (!parse_condition(it, child, depth + 1, error)) return false;
            c.items.push_back(child);
        }
        if (c.type == ConditionType::kNot && c.items.size() != 1) {
            error = "NOT takes exactly one condition";
            return false;
        }
        return true;
    }
    if (type == "time_range") {
        c.type = ConditionType::kTimeRange;
        c.start_min = parse_hhmm(str(o, "start").c_str());
        c.end_min = parse_hhmm(str(o, "end").c_str());
        if (c.start_min < 0 || c.end_min < 0) {
            error = "time_range needs start and end as HH:MM";
            return false;
        }
        return true;
    }
    if (type == "day_of_week") {
        c.type = ConditionType::kDayOfWeek;
        return parse_days(o, c.days, error);
    }
    // Comparisons: state_comparison {source,id,field,operator,value} or the
    // operator-named shorthands {type:"greater_than", source, ...}.
    CompareOp op = CompareOp::kEq;
    if (type == "state_comparison") {
        const std::string ops = str(o, "operator").empty() ? "eq" : str(o, "operator");
        if (!parse_op(ops, op)) {
            error = "unknown comparison operator '" + ops + "'";
            return false;
        }
    } else if (!parse_op(type, op)) {
        error = "unknown condition type '" + type + "'";
        return false;
    }
    c.type = ConditionType::kCompare;
    c.op = op;
    c.source = str(o, "source");
    c.id = first_str(o, {"id", "light_id", "input_id", "variable"});
    c.field = str(o, "field");
    if (c.source.empty()) {
        c.source = !str(o, "light_id").empty() ? "light" : !str(o, "input_id").empty() ? "input" : "variable";
    }
    if (c.source != "light" && c.source != "input" && c.source != "variable") {
        error = "comparison source must be light, input or variable";
        return false;
    }
    if (c.id.empty()) {
        error = "comparison needs an id";
        return false;
    }
    if (c.source == "light") {
        if (c.field.empty()) c.field = "power";
        static const char *fields[] = {"power", "brightness", "red", "green", "blue", "white", "color_temperature"};
        bool ok = false;
        for (const char *f : fields) ok |= c.field == f;
        if (!ok) {
            error = "unknown light field '" + c.field + "'";
            return false;
        }
    }
    bool bv = false;
    if (boolean(o, "value", bv)) {
        c.value = bv ? 1 : 0;
    } else if (!num(o, "value", c.value)) {
        error = "comparison needs a numeric or boolean value";
        return false;
    }
    return true;
}

cJSON *condition_json(const Condition &c)
{
    cJSON *o = cJSON_CreateObject();
    switch (c.type) {
    case ConditionType::kAnd:
    case ConditionType::kOr:
    case ConditionType::kNot: {
        cJSON_AddStringToObject(o, "type", c.type == ConditionType::kAnd ? "and" : c.type == ConditionType::kOr ? "or" : "not");
        cJSON *items = cJSON_AddArrayToObject(o, "items");
        for (const auto &ch : c.items) cJSON_AddItemToArray(items, condition_json(ch));
        break;
    }
    case ConditionType::kTimeRange:
        cJSON_AddStringToObject(o, "type", "time_range");
        cJSON_AddStringToObject(o, "start", format_hhmm(c.start_min).c_str());
        cJSON_AddStringToObject(o, "end", format_hhmm(c.end_min).c_str());
        break;
    case ConditionType::kDayOfWeek:
        cJSON_AddStringToObject(o, "type", "day_of_week");
        cJSON_AddItemToObject(o, "days", days_json(c.days));
        break;
    case ConditionType::kCompare:
        cJSON_AddStringToObject(o, "type", "state_comparison");
        cJSON_AddStringToObject(o, "source", c.source.c_str());
        cJSON_AddStringToObject(o, "id", c.id.c_str());
        if (!c.field.empty()) cJSON_AddStringToObject(o, "field", c.field.c_str());
        cJSON_AddStringToObject(o, "operator", op_name(c.op));
        cJSON_AddNumberToObject(o, "value", c.value);
        break;
    }
    return o;
}

template <std::size_t N>
bool lookup(const Name (&table)[N], const std::string &s, uint8_t &out)
{
    for (const auto &n : table) {
        if (s == n.name) {
            out = n.v;
            return true;
        }
    }
    return false;
}

} // namespace

const char *trigger_type_name(TriggerType t)
{
    for (const auto &n : kTriggers) {
        if (n.v == static_cast<uint8_t>(t)) return n.name;
    }
    return "unknown";
}

const char *action_type_name(ActionType t)
{
    for (const auto &n : kActions) {
        if (n.v == static_cast<uint8_t>(t)) return n.name;
    }
    return "unknown";
}

int16_t parse_hhmm(const char *s)
{
    int h = -1, m = -1;
    if (s == nullptr || std::sscanf(s, "%d:%d", &h, &m) != 2 || h < 0 || h > 23 || m < 0 || m > 59) {
        return -1;
    }
    return static_cast<int16_t>(h * 60 + m);
}

std::string format_hhmm(int16_t minutes)
{
    char buf[16];
    const int m = ((minutes % 1440) + 1440) % 1440;
    std::snprintf(buf, sizeof(buf), "%02d:%02d", m / 60, m % 60);
    return buf;
}

bool automation_from_json(const cJSON *obj, Automation &a, std::string &error)
{
    if (!cJSON_IsObject(obj)) {
        error = "automation must be an object";
        return false;
    }
    a = Automation{};
    a.id = str(obj, "id");
    a.name = str(obj, "name");
    if (a.id.empty() || a.id.size() > 40) {
        error = "id is required (1-40 characters)";
        return false;
    }
    for (char c : a.id) {
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-')) {
            error = "id may only contain a-z, 0-9, '_' and '-'";
            return false;
        }
    }
    if (a.name.empty() || a.name.size() > 80) {
        error = "name is required (1-80 characters)";
        return false;
    }
    boolean(obj, "enabled", a.enabled);
    const std::string mode = str(obj, "mode");
    if (mode == "advanced") a.mode = mode;

    // ---- trigger
    const cJSON *t = cJSON_GetObjectItemCaseSensitive(obj, "trigger");
    if (!cJSON_IsObject(t)) {
        error = "trigger is required";
        return false;
    }
    uint8_t tt = 0;
    const std::string ttype = str(t, "type");
    if (!lookup(kTriggers, ttype, tt)) {
        error = "unknown trigger type '" + ttype + "'";
        return false;
    }
    Trigger &tr = a.trigger;
    tr.type = static_cast<TriggerType>(tt);
    tr.target = first_str(t, {"target", "light_id", "input_id", "scene_id", "group_id", "topic", "event", "variable"});
    tr.has_state = boolean(t, "state", tr.state);
    tr.payload = str(t, "payload");
    if (num(t, "threshold", tr.threshold)) {
        tr.has_threshold = true;
        const std::string ops = str(t, "operator").empty() ? "gt" : str(t, "operator");
        if (!parse_op(ops, tr.op)) {
            error = "unknown trigger operator '" + ops + "'";
            return false;
        }
    }
    double v = 0;
    if (!parse_days(t, tr.days, error)) return false;
    switch (tr.type) {
    case TriggerType::kTimeSchedule:
        tr.minute_of_day = parse_hhmm(str(t, "time").c_str());
        if (tr.minute_of_day < 0) {
            error = "time_schedule needs time as HH:MM";
            return false;
        }
        break;
    case TriggerType::kSunrise:
    case TriggerType::kSunset:
        if (num(t, "offset_min", v)) {
            if (v < -240 || v > 240) {
                error = "offset_min must be within ±240";
                return false;
            }
            tr.offset_min = static_cast<int16_t>(v);
        }
        break;
    case TriggerType::kTimerExpired:
        if (!num(t, "interval_s", v) || v < 10 || v > 86400) {
            error = "timer_expired needs interval_s between 10 and 86400";
            return false;
        }
        tr.interval_s = static_cast<uint32_t>(v);
        break;
    case TriggerType::kLightState:
    case TriggerType::kBrightnessChange:
    case TriggerType::kColorChange:
    case TriggerType::kInputState:
    case TriggerType::kButtonPress:
    case TriggerType::kDoublePress:
    case TriggerType::kLongPress:
    case TriggerType::kSceneActivated:
    case TriggerType::kGroupStateChanged:
    case TriggerType::kMqttEvent:
    case TriggerType::kSensorThreshold:
        if (tr.target.empty()) {
            error = std::string(ttype) + " trigger needs a target";
            return false;
        }
        break;
    case TriggerType::kEspNowEvent:
    case TriggerType::kControllerStartup:
        break;
    }
    if (tr.type == TriggerType::kSensorThreshold && !tr.has_threshold) {
        error = "sensor_threshold needs threshold";
        return false;
    }

    // ---- conditions
    const cJSON *c = cJSON_GetObjectItemCaseSensitive(obj, "conditions");
    if (cJSON_IsObject(c)) {
        a.has_conditions = true;
        if (!parse_condition(c, a.conditions, 0, error)) return false;
        if (a.conditions.type != ConditionType::kAnd && a.conditions.type != ConditionType::kOr &&
            a.conditions.type != ConditionType::kNot) {
            Condition root;
            root.type = ConditionType::kAnd;
            root.items.push_back(a.conditions);
            a.conditions = root;
        }
        if (a.conditions.items.empty()) a.has_conditions = false;
    }

    // ---- actions
    const cJSON *acts = cJSON_GetObjectItemCaseSensitive(obj, "actions");
    if (!cJSON_IsArray(acts) || cJSON_GetArraySize(acts) == 0) {
        error = "at least one action is required";
        return false;
    }
    const cJSON *it = nullptr;
    int idx = 0;
    cJSON_ArrayForEach(it, acts)
    {
        if (a.actions.size() >= kMaxActions) {
            error = "too many actions (max 32)";
            return false;
        }
        const std::string at = str(it, "type");
        const std::string where = "actions[" + std::to_string(idx++) + "]: ";
        Action ac;
        uint8_t aval = 0;
        if (at == "light_power") {
            bool p = true;
            boolean(it, "power", p);
            ac.type = p ? ActionType::kLightOn : ActionType::kLightOff;
        } else if (lookup(kActions, at, aval)) {
            ac.type = static_cast<ActionType>(aval);
        } else {
            error = where + "unknown action type '" + at + "'";
            return false;
        }
        ac.target = first_str(it, {"target", "light_id", "scene_id", "group_id", "room_id", "automation_id", "variable", "output_id"});
        ac.has_power = boolean(it, "power", ac.power);
        if (num(it, "brightness", v)) {
            if (v < 0 || v > 100) {
                error = where + "brightness must be 0-100";
                return false;
            }
            ac.brightness = static_cast<int16_t>(v);
        }
        if (num(it, "red", v)) ac.red = static_cast<uint8_t>(std::fmin(255, std::fmax(0, v)));
        if (num(it, "green", v)) ac.green = static_cast<uint8_t>(std::fmin(255, std::fmax(0, v)));
        if (num(it, "blue", v)) ac.blue = static_cast<uint8_t>(std::fmin(255, std::fmax(0, v)));
        if (num(it, "color_temperature", v) || num(it, "kelvin", v)) {
            if (v < 1000 || v > 20000) {
                error = where + "color_temperature must be 1000-20000";
                return false;
            }
            ac.kelvin = static_cast<uint16_t>(v);
        }
        if (num(it, "transition_ms", v)) {
            if (v < 0 || v > 600000) {
                error = where + "transition_ms must be 0-600000";
                return false;
            }
            ac.transition_ms = static_cast<uint32_t>(v);
            ac.has_transition = true;
        }
        ac.effect = str(it, "effect");
        if (num(it, "speed_ms", v)) ac.speed_ms = static_cast<uint32_t>(std::fmax(50, v));
        if (num(it, "delay_ms", v)) {
            if (v < 0 || v > 3600000) {
                error = where + "delay_ms must be 0-3600000";
                return false;
            }
            ac.delay_ms = static_cast<uint32_t>(v);
        }
        ac.topic = first_str(it, {"topic", "host"});
        ac.payload = str(it, "payload");
        if (ac.payload.size() > 512) {
            error = where + "payload longer than 512 bytes";
            return false;
        }
        if (num(it, "port", v)) {
            if (v < 1 || v > 65535) {
                error = where + "port must be 1-65535";
                return false;
            }
            ac.port = static_cast<uint16_t>(v);
        }
        boolean(it, "retain", ac.retain);
        num(it, "value", ac.value);
        ac.value_op = str(it, "op").empty() ? "set" : str(it, "op");

        // Required fields per action.
        switch (ac.type) {
        case ActionType::kLightOn:
        case ActionType::kLightOff:
        case ActionType::kLightToggle:
        case ActionType::kRelayToggle:
        case ActionType::kSceneActivate:
        case ActionType::kExecuteAutomation:
        case ActionType::kEffectStop:
            if (ac.target.empty()) {
                error = where + at + " needs a target";
                return false;
            }
            break;
        case ActionType::kLightBrightness:
            if (ac.target.empty() || ac.brightness < 0) {
                error = where + "light_brightness needs light_id and brightness";
                return false;
            }
            break;
        case ActionType::kLightColor:
            if (ac.target.empty()) {
                error = where + "light_color needs light_id";
                return false;
            }
            break;
        case ActionType::kLightCct:
            if (ac.target.empty() || ac.kelvin == 0) {
                error = where + "light_cct needs light_id and color_temperature";
                return false;
            }
            break;
        case ActionType::kGroupControl:
        case ActionType::kRoomControl:
            if (ac.target.empty() || (!ac.has_power && ac.brightness < 0)) {
                error = where + at + " needs a target and power or brightness";
                return false;
            }
            break;
        case ActionType::kEffectRun:
            if (ac.target.empty() || ac.effect.empty()) {
                error = where + "effect_run needs light_id and effect";
                return false;
            }
            break;
        case ActionType::kDelay:
            if (ac.delay_ms == 0) {
                error = where + "delay needs delay_ms";
                return false;
            }
            break;
        case ActionType::kMqttPublish:
            if (ac.topic.empty()) {
                error = where + "mqtt_publish needs a topic";
                return false;
            }
            break;
        case ActionType::kUdpSend:
        case ActionType::kTcpSend:
            if (ac.topic.empty() || ac.port == 0) {
                error = where + at + " needs host and port";
                return false;
            }
            break;
        case ActionType::kSetVariable:
            if (ac.target.empty() || ac.target.size() > 24 || (ac.value_op != "set" && ac.value_op != "add")) {
                error = where + "set_variable needs variable (<=24 chars) and op set|add";
                return false;
            }
            break;
        }
        a.actions.push_back(ac);
    }

    // ---- execution
    const cJSON *ex = cJSON_GetObjectItemCaseSensitive(obj, "execution");
    if (cJSON_IsObject(ex)) {
        if (num(ex, "cooldown_ms", v)) a.execution.cooldown_ms = static_cast<uint32_t>(std::fmax(0, std::fmin(v, 86400000)));
        if (num(ex, "max_executions_per_hour", v)) {
            if (v < 1 || v > 3600) {
                error = "max_executions_per_hour must be 1-3600";
                return false;
            }
            a.execution.max_per_hour = static_cast<uint16_t>(v);
        }
        boolean(ex, "allow_parallel", a.execution.allow_parallel);
    }
    return true;
}

cJSON *automation_to_json(const Automation &a)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddNumberToObject(o, "schema_version", 1);
    cJSON_AddStringToObject(o, "id", a.id.c_str());
    cJSON_AddStringToObject(o, "name", a.name.c_str());
    cJSON_AddBoolToObject(o, "enabled", a.enabled);
    cJSON_AddStringToObject(o, "mode", a.mode.c_str());

    cJSON *t = cJSON_AddObjectToObject(o, "trigger");
    const Trigger &tr = a.trigger;
    cJSON_AddStringToObject(t, "type", trigger_type_name(tr.type));
    if (!tr.target.empty()) cJSON_AddStringToObject(t, "target", tr.target.c_str());
    if (tr.has_state) cJSON_AddBoolToObject(t, "state", tr.state);
    if (tr.has_threshold) {
        cJSON_AddNumberToObject(t, "threshold", tr.threshold);
        cJSON_AddStringToObject(t, "operator", op_name(tr.op));
    }
    if (!tr.payload.empty()) cJSON_AddStringToObject(t, "payload", tr.payload.c_str());
    if (tr.type == TriggerType::kTimeSchedule) cJSON_AddStringToObject(t, "time", format_hhmm(tr.minute_of_day).c_str());
    if (tr.type == TriggerType::kSunrise || tr.type == TriggerType::kSunset) cJSON_AddNumberToObject(t, "offset_min", tr.offset_min);
    if (tr.type == TriggerType::kTimerExpired) cJSON_AddNumberToObject(t, "interval_s", tr.interval_s);
    if (tr.days != 0x7F) cJSON_AddItemToObject(t, "days", days_json(tr.days));

    if (a.has_conditions) {
        cJSON *root = condition_json(a.conditions);
        cJSON *op = cJSON_DetachItemFromObjectCaseSensitive(root, "type");
        if (op != nullptr) {
            std::string up = op->valuestring;
            for (auto &ch : up) ch = static_cast<char>(std::toupper(ch));
            cJSON_AddStringToObject(root, "operator", up.c_str());
            cJSON_Delete(op);
        }
        cJSON_AddItemToObject(o, "conditions", root);
    }

    cJSON *arr = cJSON_AddArrayToObject(o, "actions");
    for (const auto &ac : a.actions) {
        cJSON *e = cJSON_CreateObject();
        cJSON_AddStringToObject(e, "type", action_type_name(ac.type));
        if (!ac.target.empty()) cJSON_AddStringToObject(e, "target", ac.target.c_str());
        if (ac.has_power) cJSON_AddBoolToObject(e, "power", ac.power);
        if (ac.brightness >= 0) cJSON_AddNumberToObject(e, "brightness", ac.brightness);
        if (ac.type == ActionType::kLightColor) {
            cJSON_AddNumberToObject(e, "red", ac.red);
            cJSON_AddNumberToObject(e, "green", ac.green);
            cJSON_AddNumberToObject(e, "blue", ac.blue);
        }
        if (ac.kelvin) cJSON_AddNumberToObject(e, "color_temperature", ac.kelvin);
        if (ac.has_transition) cJSON_AddNumberToObject(e, "transition_ms", ac.transition_ms);
        if (!ac.effect.empty()) cJSON_AddStringToObject(e, "effect", ac.effect.c_str());
        if (ac.speed_ms) cJSON_AddNumberToObject(e, "speed_ms", ac.speed_ms);
        if (ac.delay_ms) cJSON_AddNumberToObject(e, "delay_ms", ac.delay_ms);
        if (!ac.topic.empty()) cJSON_AddStringToObject(e, ac.type == ActionType::kMqttPublish ? "topic" : "host", ac.topic.c_str());
        if (!ac.payload.empty()) cJSON_AddStringToObject(e, "payload", ac.payload.c_str());
        if (ac.port) cJSON_AddNumberToObject(e, "port", ac.port);
        if (ac.retain) cJSON_AddBoolToObject(e, "retain", true);
        if (ac.type == ActionType::kSetVariable) {
            cJSON_AddNumberToObject(e, "value", ac.value);
            cJSON_AddStringToObject(e, "op", ac.value_op.c_str());
        }
        cJSON_AddItemToArray(arr, e);
    }

    cJSON *ex = cJSON_AddObjectToObject(o, "execution");
    cJSON_AddNumberToObject(ex, "cooldown_ms", a.execution.cooldown_ms);
    cJSON_AddNumberToObject(ex, "max_executions_per_hour", a.execution.max_per_hour);
    cJSON_AddBoolToObject(ex, "allow_parallel", a.execution.allow_parallel);
    return o;
}

std::string automation_summary(const Automation &a)
{
    const Trigger &t = a.trigger;
    std::string s = "When ";
    switch (t.type) {
    case TriggerType::kLightState: s += t.target + (t.has_state ? (t.state ? " turns on" : " turns off") : " changes"); break;
    case TriggerType::kBrightnessChange:
        s += t.target + " brightness " + (t.has_threshold ? std::string(op_symbol(t.op)) + " " + std::to_string(static_cast<int>(t.threshold)) + "%" : "changes");
        break;
    case TriggerType::kColorChange: s += t.target + " colour changes"; break;
    case TriggerType::kInputState: s += "input " + t.target + (t.has_state ? (t.state ? " turns on" : " turns off") : " changes"); break;
    case TriggerType::kButtonPress: s += t.target + " is pressed"; break;
    case TriggerType::kDoublePress: s += t.target + " is double-pressed"; break;
    case TriggerType::kLongPress: s += t.target + " is held"; break;
    case TriggerType::kTimeSchedule: s += "it is " + format_hhmm(t.minute_of_day); break;
    case TriggerType::kSunrise:
    case TriggerType::kSunset: {
        const char *ev = t.type == TriggerType::kSunrise ? "sunrise" : "sunset";
        s += t.offset_min == 0 ? std::string("it is ") + ev
                               : std::to_string(std::abs(t.offset_min)) + " min " + (t.offset_min < 0 ? "before " : "after ") + ev;
        break;
    }
    case TriggerType::kSensorThreshold: s += t.target + " " + op_symbol(t.op) + " " + fmt_num(t.threshold); break;
    case TriggerType::kMqttEvent: s += "MQTT " + t.target + " arrives"; break;
    case TriggerType::kEspNowEvent: s += "a peer controller event arrives"; break;
    case TriggerType::kSceneActivated: s += "scene " + t.target + " is activated"; break;
    case TriggerType::kGroupStateChanged: s += "group " + t.target + " changes"; break;
    case TriggerType::kControllerStartup: s += "the controller starts"; break;
    case TriggerType::kTimerExpired: s += "every " + std::to_string(t.interval_s) + " s"; break;
    }
    if (a.has_conditions) s += " (if conditions match)";
    s += ", ";
    for (std::size_t i = 0; i < a.actions.size() && i < 3; ++i) {
        const Action &ac = a.actions[i];
        if (i) s += ", then ";
        switch (ac.type) {
        case ActionType::kLightOn: s += "turn on " + ac.target; break;
        case ActionType::kLightOff: s += "turn off " + ac.target; break;
        case ActionType::kLightToggle: s += "toggle " + ac.target; break;
        case ActionType::kLightBrightness: s += "set " + ac.target + " to " + std::to_string(ac.brightness) + "%"; break;
        case ActionType::kLightColor: s += "colour " + ac.target; break;
        case ActionType::kLightCct: s += "set " + ac.target + " to " + std::to_string(ac.kelvin) + " K"; break;
        case ActionType::kSceneActivate: s += "activate " + ac.target; break;
        case ActionType::kGroupControl: s += "control group " + ac.target; break;
        case ActionType::kRoomControl: s += "control room " + ac.target; break;
        case ActionType::kRelayToggle: s += "toggle " + ac.target; break;
        case ActionType::kEffectRun: s += "run " + ac.effect + " on " + ac.target; break;
        case ActionType::kEffectStop: s += "stop effect on " + ac.target; break;
        case ActionType::kDelay: s += "wait " + std::to_string(ac.delay_ms / 1000) + " s"; break;
        case ActionType::kMqttPublish: s += "publish to " + ac.topic; break;
        case ActionType::kUdpSend: s += "send UDP to " + ac.topic; break;
        case ActionType::kTcpSend: s += "send TCP to " + ac.topic; break;
        case ActionType::kExecuteAutomation: s += "run " + ac.target; break;
        case ActionType::kSetVariable: s += (ac.value_op == "add" ? "add to " : "set ") + ac.target; break;
        }
    }
    if (a.actions.size() > 3) s += " …";
    return s;
}

} // namespace lumen
