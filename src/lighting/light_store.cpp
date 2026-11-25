// Lumen — light configuration store. See light_store.h
#include "lumen/lighting/light_store.h"

#include <cstring>

namespace lumen
{
namespace light_store
{

namespace
{

std::string get_str(const cJSON *obj, const char *key)
{
    const cJSON *it = cJSON_GetObjectItemCaseSensitive(obj, key);
    return (cJSON_IsString(it) && it->valuestring != nullptr) ? std::string(it->valuestring)
                                                              : std::string();
}

bool get_num(const cJSON *obj, const char *key, double &out)
{
    const cJSON *it = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (!cJSON_IsNumber(it)) {
        return false;
    }
    out = it->valuedouble;
    return true;
}

bool driver_type_known(const std::string &t)
{
    return t == "pca9685" || t == "ledc" || t == "relay" || t == "gpio" || t == "rmt";
}

// First/last output a channel occupies on its device.
void output_span(const Channel &ch, int &first, int &last)
{
    first = ch.driver.channel;
    last = first;
    if (ch.driver.type == "pca9685") {
        last = first + light_type_output_count(ch.type) - 1;
    }
}

} // namespace

void register_documents(ConfigManager &config)
{
    DocConfig lights{kLightsDoc, 1, migrate_additive, nullptr, nullptr};
    config.register_document(lights);
    DocConfig state{kStateDoc, 1, migrate_additive, nullptr, nullptr};
    config.register_document(state);
}

bool valid_id(const std::string &id)
{
    if (id.empty() || id.size() > 48) {
        return false;
    }
    for (char c : id) {
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-')) {
            return false;
        }
    }
    return true;
}

bool channel_from_json(const cJSON *obj, Channel &out, std::string &error)
{
    if (!cJSON_IsObject(obj)) {
        error = "light must be an object";
        return false;
    }
    out.id = get_str(obj, "id");
    if (!valid_id(out.id)) {
        error = "id must be 1-48 characters of a-z, 0-9, '_' or '-'";
        return false;
    }
    out.name = get_str(obj, "name");
    if (out.name.empty()) {
        out.name = out.id;
    }
    if (out.name.size() > 64) {
        error = "name is longer than 64 characters";
        return false;
    }
    const std::string type = get_str(obj, "type");
    if (!light_type_known(type.c_str())) {
        error = "unknown light type '" + type + "'";
        return false;
    }
    out.type = light_type_from_string(type.c_str());
    out.room_id = get_str(obj, "roomId");
    if (out.room_id.empty()) {
        out.room_id = get_str(obj, "room_id");
    }

    const cJSON *driver = cJSON_GetObjectItemCaseSensitive(obj, "driver");
    if (!cJSON_IsObject(driver)) {
        error = "driver {type, deviceId, channel} is required";
        return false;
    }
    out.driver.type = get_str(driver, "type");
    out.driver.device_id = get_str(driver, "deviceId");
    if (out.driver.device_id.empty()) {
        out.driver.device_id = get_str(driver, "device_id");
    }
    double ch = 0;
    out.driver.channel = get_num(driver, "channel", ch) ? static_cast<int32_t>(ch) : 0;
    if (!driver_type_known(out.driver.type)) {
        error = "unknown driver type '" + out.driver.type + "'";
        return false;
    }
    if (out.driver.device_id.empty()) {
        error = "driver.deviceId is required";
        return false;
    }
    if (out.driver.channel < 0 || out.driver.channel > 63) {
        error = "driver.channel out of range";
        return false;
    }

    double num = 0;
    if (get_num(obj, "transitionMs", num) || get_num(obj, "transition_ms", num)) {
        if (num < 0 || num > 600000) {
            error = "transitionMs must be 0..600000";
            return false;
        }
        out.transition_ms = static_cast<uint32_t>(num);
    }
    const std::string startup = get_str(obj, "startup");
    if (!startup.empty()) {
        if (startup != "restore" && startup != "on" && startup != "off") {
            error = "startup must be restore, on or off";
            return false;
        }
        out.startup = startup_behavior_from_string(startup.c_str());
    }

    const cJSON *limits = cJSON_GetObjectItemCaseSensitive(obj, "limits");
    if (cJSON_IsObject(limits)) {
        if (get_num(limits, "minBrightness", num)) out.limits.minimum = static_cast<uint8_t>(clamp(static_cast<int>(num), 0, 100));
        if (get_num(limits, "maxBrightness", num)) out.limits.maximum = static_cast<uint8_t>(clamp(static_cast<int>(num), 1, 100));
        if (get_num(limits, "colorTemperatureMin", num)) out.limits.color_temperature_min = static_cast<uint16_t>(clamp(static_cast<int>(num), 1000, 20000));
        if (get_num(limits, "colorTemperatureMax", num)) out.limits.color_temperature_max = static_cast<uint16_t>(clamp(static_cast<int>(num), 1000, 20000));
    }
    if (out.limits.minimum > out.limits.maximum ||
        out.limits.color_temperature_min >= out.limits.color_temperature_max) {
        error = "limits: minimum must be below maximum";
        return false;
    }
    const cJSON *cal = cJSON_GetObjectItemCaseSensitive(obj, "calibration");
    if (cJSON_IsObject(cal)) {
        auto factor = [&](const char *key, float &dst) {
            double v = 0;
            if (get_num(cal, key, v)) dst = static_cast<float>(v < 0 ? 0 : (v > 2 ? 2 : v));
        };
        factor("red", out.calibration.red);
        factor("green", out.calibration.green);
        factor("blue", out.calibration.blue);
        factor("white", out.calibration.white);
    }
    return true;
}

cJSON *state_to_json(const LightState &s)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddBoolToObject(o, "power", s.power);
    cJSON_AddNumberToObject(o, "brightness", s.brightness);
    cJSON_AddNumberToObject(o, "red", s.red);
    cJSON_AddNumberToObject(o, "green", s.green);
    cJSON_AddNumberToObject(o, "blue", s.blue);
    cJSON_AddNumberToObject(o, "white", s.white);
    cJSON_AddNumberToObject(o, "colorTemperature", s.color_temperature);
    return o;
}

cJSON *channel_to_json(const Channel &ch, bool include_state)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "id", ch.id.c_str());
    cJSON_AddStringToObject(o, "name", ch.name.c_str());
    cJSON_AddStringToObject(o, "type", light_type_name(ch.type));
    cJSON_AddStringToObject(o, "roomId", ch.room_id.c_str());
    cJSON *drv = cJSON_AddObjectToObject(o, "driver");
    cJSON_AddStringToObject(drv, "type", ch.driver.type.c_str());
    cJSON_AddStringToObject(drv, "deviceId", ch.driver.device_id.c_str());
    cJSON_AddNumberToObject(drv, "channel", ch.driver.channel);
    cJSON_AddNumberToObject(o, "transitionMs", ch.transition_ms);
    cJSON_AddStringToObject(o, "startup", startup_behavior_name(ch.startup));
    cJSON *lim = cJSON_AddObjectToObject(o, "limits");
    cJSON_AddNumberToObject(lim, "minBrightness", ch.limits.minimum);
    cJSON_AddNumberToObject(lim, "maxBrightness", ch.limits.maximum);
    cJSON_AddNumberToObject(lim, "colorTemperatureMin", ch.limits.color_temperature_min);
    cJSON_AddNumberToObject(lim, "colorTemperatureMax", ch.limits.color_temperature_max);
    cJSON *cal = cJSON_AddObjectToObject(o, "calibration");
    cJSON_AddNumberToObject(cal, "red", ch.calibration.red);
    cJSON_AddNumberToObject(cal, "green", ch.calibration.green);
    cJSON_AddNumberToObject(cal, "blue", ch.calibration.blue);
    cJSON_AddNumberToObject(cal, "white", ch.calibration.white);
    if (include_state) {
        // "state" is the target the user asked for (what the UI shows);
        // "output" is what the hardware shows right now (moves during
        // transitions/effects).
        cJSON_AddItemToObject(o, "state", state_to_json(ch.requested));
        cJSON_AddItemToObject(o, "output", state_to_json(ch.applied));
    }
    return o;
}

bool command_from_json(const cJSON *obj, LightCommand &cmd, std::string &error)
{
    if (!cJSON_IsObject(obj)) {
        error = "command must be an object";
        return false;
    }
    cmd.fields = 0;
    const cJSON *power = cJSON_GetObjectItemCaseSensitive(obj, "power");
    if (power != nullptr) {
        if (!cJSON_IsBool(power)) {
            error = "power must be a boolean";
            return false;
        }
        cmd.state.power = cJSON_IsTrue(power);
        cmd.fields |= kFieldPower;
    }
    // "toggle": true is resolved by the caller (needs the current state).
    const cJSON *toggle = cJSON_GetObjectItemCaseSensitive(obj, "toggle");
    struct Field
    {
        const char *key;
        int lo, hi;
        uint8_t flag;
    };
    const Field fields[] = {
        {"brightness", 0, 100, kFieldBrightness}, {"red", 0, 255, kFieldColor},
        {"green", 0, 255, kFieldColor},           {"blue", 0, 255, kFieldColor},
        {"white", 0, 255, kFieldWhite},           {"colorTemperature", 1000, 20000, kFieldCct},
    };
    for (const auto &f : fields) {
        double v = 0;
        const cJSON *it = cJSON_GetObjectItemCaseSensitive(obj, f.key);
        if (it == nullptr) {
            continue;
        }
        if (!get_num(obj, f.key, v) || v < f.lo || v > f.hi) {
            error = std::string(f.key) + " must be a number " + std::to_string(f.lo) + ".." +
                    std::to_string(f.hi);
            return false;
        }
        const int iv = static_cast<int>(v);
        if (std::strcmp(f.key, "brightness") == 0) cmd.state.brightness = static_cast<uint8_t>(iv);
        else if (std::strcmp(f.key, "red") == 0) cmd.state.red = static_cast<uint8_t>(iv);
        else if (std::strcmp(f.key, "green") == 0) cmd.state.green = static_cast<uint8_t>(iv);
        else if (std::strcmp(f.key, "blue") == 0) cmd.state.blue = static_cast<uint8_t>(iv);
        else if (std::strcmp(f.key, "white") == 0) cmd.state.white = static_cast<uint8_t>(iv);
        else cmd.state.color_temperature = static_cast<uint16_t>(iv);
        cmd.fields |= f.flag;
    }
    double t = 0;
    if (cJSON_GetObjectItemCaseSensitive(obj, "transitionMs") != nullptr) {
        if (!get_num(obj, "transitionMs", t) || t < 0 || t > 600000) {
            error = "transitionMs must be 0..600000";
            return false;
        }
        cmd.transition_ms = static_cast<uint32_t>(t);
        cmd.use_default_transition = false;
    } else {
        cmd.use_default_transition = true;
    }
    const std::string effect = get_str(obj, "effect");
    if (!effect.empty()) {
        EffectType et{};
        if (effect != "stop" && effect != "none" && !effect_type_from_string(effect, et)) {
            error = "unknown effect '" + effect + "'";
            return false;
        }
        cmd.effect = effect;
        const cJSON *params = cJSON_GetObjectItemCaseSensitive(obj, "effectParams");
        if (cJSON_IsObject(params)) {
            double v = 0;
            if (get_num(params, "speedMs", v) && v >= 50 && v <= 600000) cmd.effect_speed_ms = static_cast<uint32_t>(v);
            if (get_num(params, "durationMs", v) && v >= 0 && v <= 86400000) cmd.effect_duration_ms = static_cast<uint32_t>(v);
        }
    }
    const std::string origin = get_str(obj, "origin");
    if (!origin.empty()) cmd.origin = origin.substr(0, 16);
    const std::string command_id = get_str(obj, "commandId");
    if (!command_id.empty()) cmd.command_id = command_id.substr(0, 40);
    if (cmd.fields == 0 && cmd.effect.empty() && !cJSON_IsTrue(toggle)) {
        error = "command sets no fields";
        return false;
    }
    return true;
}

bool validate_against(const Channel &candidate, const BoardProfile &profile,
                      const ChannelManager &channels, const std::string &ignore_id,
                      std::string &error)
{
    // The referenced device must exist in the active profile with a
    // matching kind, and the outputs must fit on it.
    const std::string &dev = candidate.driver.device_id;
    const int outputs = light_type_output_count(candidate.type);
    bool found = false;
    if (candidate.driver.type == "pca9685") {
        for (const auto &p : profile.peripherals) {
            if (p.id == dev && p.type == "pca9685") {
                const int count = p.channels > 0 ? p.channels : 16;
                if (candidate.driver.channel + outputs > count) {
                    error = "outputs " + std::to_string(candidate.driver.channel + 1) + ".." +
                            std::to_string(candidate.driver.channel + outputs) +
                            " exceed the " + std::to_string(count) + " channels of '" + dev + "'";
                    return false;
                }
                found = true;
            }
        }
    } else if (candidate.driver.type == "relay") {
        for (const auto &p : profile.peripherals) {
            if (p.id == dev && p.type == "relay") found = true;
        }
        if (found && candidate.type != LightType::kSwitch) {
            error = "a relay can only drive a 'switch' light";
            return false;
        }
    } else if (candidate.driver.type == "ledc" || candidate.driver.type == "gpio") {
        for (const auto &r : profile.resources) {
            if (r.id == dev && ((candidate.driver.type == "ledc" && r.type == "pwm") ||
                                (candidate.driver.type == "gpio" && r.type == "gpio" &&
                                 r.direction == "output"))) {
                found = true;
            }
        }
        if (found && outputs != 1) {
            error = "a single PWM/GPIO output cannot drive a multi-channel light";
            return false;
        }
        if (found && candidate.driver.type == "gpio" && candidate.type != LightType::kSwitch) {
            error = "a GPIO output can only drive a 'switch' light";
            return false;
        }
    } else if (candidate.driver.type == "rmt") {
        for (const auto &p : profile.peripherals) {
            if (p.id == dev && p.type == "addressable_led") found = true;
        }
    }
    if (!found) {
        error = "device '" + dev + "' (" + candidate.driver.type +
                ") is not in the active board profile";
        return false;
    }

    int first = 0, last = 0;
    output_span(candidate, first, last);
    for (std::size_t i = 0; i < channels.channel_count(); ++i) {
        const Channel &other = channels.at(i);
        if (other.id == ignore_id || other.driver.device_id != dev) {
            continue;
        }
        int of = 0, ol = 0;
        output_span(other, of, ol);
        if (first <= ol && of <= last) {
            error = "output already used by light '" + other.name + "' (" + other.id + ")";
            return false;
        }
    }
    return true;
}

ConfigStatus load_lights(ConfigManager &config, LightingTask &lighting)
{
    std::string contents;
    const ConfigStatus st = config.load_document(kLightsDoc, contents);
    if (st != ConfigStatus::kOk) {
        return st == ConfigStatus::kNotFound ? ConfigStatus::kOk : st;
    }
    cJSON *root = cJSON_Parse(contents.c_str());
    if (root == nullptr) {
        return ConfigStatus::kCorrupt;
    }
    const cJSON *arr = cJSON_GetObjectItemCaseSensitive(root, "lights");
    const cJSON *item = nullptr;
    cJSON_ArrayForEach(item, arr)
    {
        Channel ch;
        std::string error;
        if (channel_from_json(item, ch, error)) {
            lighting.register_channel(ch);
        }
    }
    cJSON_Delete(root);
    return ConfigStatus::kOk;
}

ConfigStatus save_lights(ConfigManager &config, const LightingTask &lighting)
{
    cJSON *root = cJSON_CreateObject();
    cJSON *arr = cJSON_AddArrayToObject(root, "lights");
    const ChannelManager &cm = lighting.channel_manager();
    for (std::size_t i = 0; i < cm.channel_count(); ++i) {
        cJSON_AddItemToArray(arr, channel_to_json(cm.at(i), false));
    }
    char *text = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (text == nullptr) {
        return ConfigStatus::kIoError;
    }
    const ConfigStatus st = config.save_document(kLightsDoc, text);
    cJSON_free(text);
    return st;
}

ConfigStatus load_states(ConfigManager &config, LightingTask &lighting)
{
    std::string contents;
    cJSON *root = nullptr;
    if (config.load_document(kStateDoc, contents) == ConfigStatus::kOk) {
        root = cJSON_Parse(contents.c_str());
    }
    const cJSON *states = root ? cJSON_GetObjectItemCaseSensitive(root, "states") : nullptr;
    ChannelManager &cm = lighting.channel_manager();
    for (std::size_t i = 0; i < cm.channel_count(); ++i) {
        const Channel &ch = cm.at(i);
        LightState s{};
        const cJSON *saved = states ? cJSON_GetObjectItemCaseSensitive(states, ch.id.c_str()) : nullptr;
        if (cJSON_IsObject(saved)) {
            double v = 0;
            s.power = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(saved, "power"));
            if (get_num(saved, "brightness", v)) s.brightness = static_cast<uint8_t>(clamp(static_cast<int>(v), 0, 100));
            if (get_num(saved, "red", v)) s.red = static_cast<uint8_t>(clamp(static_cast<int>(v), 0, 255));
            if (get_num(saved, "green", v)) s.green = static_cast<uint8_t>(clamp(static_cast<int>(v), 0, 255));
            if (get_num(saved, "blue", v)) s.blue = static_cast<uint8_t>(clamp(static_cast<int>(v), 0, 255));
            if (get_num(saved, "white", v)) s.white = static_cast<uint8_t>(clamp(static_cast<int>(v), 0, 255));
            if (get_num(saved, "colorTemperature", v)) s.color_temperature = static_cast<uint16_t>(clamp(static_cast<int>(v), 0, 20000));
        }
        switch (ch.startup) {
        case StartupBehavior::kOff:
            s.power = false;
            break;
        case StartupBehavior::kOn:
            s.power = true;
            if (s.brightness == 0) s.brightness = 100;
            break;
        case StartupBehavior::kRestore:
            break;
        }
        lighting.restore_state(ch.id, s);
    }
    cJSON_Delete(root);
    return ConfigStatus::kOk;
}

ConfigStatus save_states(ConfigManager &config, const LightingTask &lighting)
{
    cJSON *root = cJSON_CreateObject();
    cJSON *states = cJSON_AddObjectToObject(root, "states");
    const ChannelManager &cm = lighting.channel_manager();
    for (std::size_t i = 0; i < cm.channel_count(); ++i) {
        cJSON_AddItemToObject(states, cm.at(i).id.c_str(), state_to_json(cm.at(i).requested));
    }
    char *text = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (text == nullptr) {
        return ConfigStatus::kIoError;
    }
    const ConfigStatus st = config.save_document(kStateDoc, text);
    cJSON_free(text);
    return st;
}

} // namespace light_store
} // namespace lumen
