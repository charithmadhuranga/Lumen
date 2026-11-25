// Lumen — automation host: the engine's view of the running system, built
// on the app context. Every call happens with lumen::model_mutex() held
// (engine ticks run in the lighting task under the lock).
#include "lumen/app/automation_host.hpp"

#include <ctime>

#include "esp_timer.h"
#include "lumen/hardware/hardware_manager.h"

namespace lumen
{

bool AppAutomationHost::light_state(const std::string &id, LightState &out)
{
    const Channel *ch = ctx_.lighting->channel_manager().find(id);
    if (ch == nullptr) return false;
    out = ch->requested;
    return true;
}

bool AppAutomationHost::input_state(const std::string &id, bool &out)
{
    for (const auto &in : ctx_.hardware->inputs()) {
        if (in.id == id || in.name == id) {
            out = in.state;
            return true;
        }
    }
    return false;
}

bool AppAutomationHost::exists(const char *kind, const std::string &id)
{
    const std::string k = kind;
    if (k == "light") return ctx_.lighting->channel_manager().find(id) != nullptr;
    if (k == "scene") return ctx_.scenes->find(id) != nullptr;
    if (k == "group") return ctx_.groups->find(id) != nullptr;
    if (k == "room") return ctx_.rooms->find(id) != nullptr;
    if (k == "input") {
        bool v = false;
        return input_state(id, v);
    }
    if (k == "output") {
        for (const auto &o : ctx_.hardware->outputs()) {
            if (o.id == id) return true;
        }
    }
    return false;
}

bool AppAutomationHost::light_command(const LightCommand &cmd)
{
    LightCommandEnvelope env;
    env.command = cmd;
    env.posted_ms = static_cast<uint32_t>(esp_timer_get_time() / 1000);
    return ctx_.lighting->enqueue(env) == EnqueueResult::kOk;
}

bool AppAutomationHost::scene_activate(const std::string &id, const std::string &origin)
{
    const bool ok = ctx_.scenes->activate_scene(id, 0, origin) == SceneResult::kOk;
    if (ok) {
        ctx_.ws->broadcast("scene_activated", "{\"sceneId\":\"" + id + "\"}", false);
        AutoEvent e;
        e.type = AutoEventType::kScene;
        e.id = id;
        e.origin = origin;
        ctx_.automation->post(e);
    }
    return ok;
}

bool AppAutomationHost::group_command(const std::string &id, bool is_room, const LightCommand &cmd)
{
    std::vector<std::string> members;
    if (is_room) {
        const Room *r = ctx_.rooms->find(id);
        if (r == nullptr) return false;
        members = r->channel_ids;
    } else {
        const Group *g = ctx_.groups->find(id);
        if (g == nullptr) return false;
        members = g->channel_ids;
        AutoEvent e;
        e.type = AutoEventType::kGroup;
        e.id = id;
        e.origin = cmd.origin;
        ctx_.automation->post(e);
    }
    bool ok = true;
    for (const auto &m : members) {
        LightCommand c = cmd;
        c.channel_id = m;
        const Channel *ch = ctx_.lighting->channel_manager().find(m);
        if (ch != nullptr && ch->type == LightType::kSwitch) c.fields &= static_cast<uint8_t>(~kFieldBrightness);
        if (c.fields == 0) continue;
        ok &= light_command(c);
    }
    return ok;
}

bool AppAutomationHost::output_toggle(const std::string &output_id)
{
    for (const auto &o : ctx_.hardware->outputs()) {
        if (o.id == output_id) {
            return ctx_.hardware->set_output(output_id, o.on ? 0 : 65535) == ESP_OK;
        }
    }
    return false;
}

bool AppAutomationHost::mqtt_publish(const std::string &topic, const std::string &payload, bool retain)
{
    return ctx_.mqtt_publish ? ctx_.mqtt_publish(topic, payload, retain) : false;
}

bool AppAutomationHost::udp_send(const std::string &host, uint16_t port, const std::string &payload)
{
    return ctx_.net_send ? ctx_.net_send(host, port, payload, false) : false;
}

bool AppAutomationHost::tcp_send(const std::string &host, uint16_t port, const std::string &payload)
{
    return ctx_.net_send ? ctx_.net_send(host, port, payload, true) : false;
}

AutoClock AppAutomationHost::clock()
{
    AutoClock c;
    const std::time_t now = std::time(nullptr);
    if (now < 1700000000) {
        return c; // SNTP not synced yet: time-based automations wait
    }
    std::tm local{};
    localtime_r(&now, &local);
    std::tm utc{};
    gmtime_r(&now, &utc);
    c.valid = true;
    c.unix_time = static_cast<int64_t>(now);
    c.minute_of_day = local.tm_hour * 60 + local.tm_min;
    c.weekday = (local.tm_wday + 6) % 7; // tm: 0 = Sunday -> 0 = Monday
    c.day_id = (local.tm_year + 1900) * 400 + local.tm_yday;
    if (has_location_) {
        if (sun_day_ != c.day_id) {
            // Local offset from UTC in minutes (handles DST via the TZ rule).
            int offset = (local.tm_hour - utc.tm_hour) * 60 + (local.tm_min - utc.tm_min);
            const int day_diff = local.tm_yday - utc.tm_yday;
            if (day_diff == 1 || day_diff < -1) offset += 1440;
            if (day_diff == -1 || day_diff > 1) offset -= 1440;
            sun_day_ = c.day_id;
            if (!sun_times(local.tm_year + 1900, local.tm_mon + 1, local.tm_mday, latitude_, longitude_, offset,
                           sunrise_, sunset_)) {
                sunrise_ = sunset_ = -1;
            }
        }
        c.sunrise_min = sunrise_;
        c.sunset_min = sunset_;
    }
    return c;
}

void AppAutomationHost::set_location(double lat, double lon)
{
    latitude_ = lat;
    longitude_ = lon;
    has_location_ = true;
    sun_day_ = -1;
}

bool AppAutomationHost::location(double &lat, double &lon) const
{
    lat = latitude_;
    lon = longitude_;
    return has_location_;
}

} // namespace lumen
