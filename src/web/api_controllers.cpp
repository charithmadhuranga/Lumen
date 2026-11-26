// Lumen API — multi-controller (ESP-NOW, Phase 11).
//
//   GET  /controllers                                  this controller, peers (+ their lights), counters
//   POST /controllers/{id}/lights/{light}/command      Lumen light command for a peer's light
//   POST /controllers/scenes/{scene}/activate          activate locally and on every peer with that scene
//   PUT  /integrations/espnow                          {enabled, networkId, key?}
//   GET  /integrations/espnow/key                      network key (signed-in user)
//   POST /integrations/espnow/key/regenerate
#include "esp_timer.h"
#include "lumen/app/app_context.hpp"
#include "lumen/core/model_lock.hpp"
#include "lumen/networking/espnow_manager.hpp"
#include "lumen/web/api_util.hpp"

namespace lumen
{

namespace
{

cJSON *state_json(const LightState &s)
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

cJSON *controllers_json(EspNowManager &m)
{
    const EspNowSettings s = m.settings();
    const EspNowStats st = m.stats();
    cJSON *o = cJSON_CreateObject();
    cJSON *self = cJSON_AddObjectToObject(o, "self");
    cJSON_AddStringToObject(self, "id", m.controller_id().c_str());
    cJSON_AddBoolToObject(self, "enabled", s.enabled);
    cJSON_AddBoolToObject(self, "running", m.running());
    cJSON_AddStringToObject(self, "networkId", s.network_id.c_str());
    cJSON_AddNumberToObject(self, "maxPeers", static_cast<double>(EspNowManager::kMaxPeers));
    cJSON_AddBoolToObject(self, "hasKey", !m.key().empty());
    cJSON *peers = cJSON_AddArrayToObject(o, "peers");
    const uint32_t now = static_cast<uint32_t>(esp_timer_get_time() / 1000);
    for (const auto &p : m.peers()) {
        if (p.id.empty()) continue;
        cJSON *j = cJSON_CreateObject();
        cJSON_AddStringToObject(j, "id", p.id.c_str());
        cJSON_AddStringToObject(j, "ip", p.ip.c_str());
        cJSON_AddStringToObject(j, "version", p.version.c_str());
        cJSON_AddBoolToObject(j, "online", p.online);
        cJSON_AddNumberToObject(j, "lastSeenSec", (now - p.last_seen_ms) / 1000);
        cJSON_AddNumberToObject(j, "rssi", p.rssi);
        cJSON *ls = cJSON_AddArrayToObject(j, "lights");
        for (const auto &l : p.lights) {
            cJSON *lj = cJSON_CreateObject();
            cJSON_AddStringToObject(lj, "id", l.id.c_str());
            cJSON_AddStringToObject(lj, "name", l.name.c_str());
            cJSON_AddStringToObject(lj, "type", l.type.c_str());
            cJSON_AddItemToObject(lj, "output", state_json(l.state));
            cJSON_AddItemToArray(ls, lj);
        }
        cJSON_AddItemToArray(peers, j);
    }
    cJSON *c = cJSON_AddObjectToObject(o, "stats");
    const std::pair<const char *, uint32_t> fields[] = {
        {"tx", st.tx}, {"txFailed", st.tx_failed}, {"rx", st.rx}, {"rxQueueFull", st.rx_queue_full},
        {"malformed", st.malformed}, {"versionMismatch", st.version}, {"otherNetwork", st.network}, {"authFailed", st.auth},
        {"replayed", st.replay}, {"duplicates", st.duplicate}, {"stale", st.stale}, {"peersFull", st.peers_full},
        {"commandsSent", st.commands_sent}, {"commandsApplied", st.commands_applied}, {"commandsRejected", st.commands_rejected},
        {"scenesApplied", st.scenes_applied}};
    for (const auto &f : fields) cJSON_AddNumberToObject(c, f.first, f.second);
    return o;
}

// Lumen REST light command -> the HA/MQTT field set the owner parses.
cJSON *to_ha(const cJSON *b, std::string &error)
{
    cJSON *o = cJSON_CreateObject();
    double v = 0;
    bool any = false;
    const cJSON *power = cJSON_GetObjectItemCaseSensitive(b, "power");
    if (cJSON_IsBool(power)) {
        cJSON_AddStringToObject(o, "state", cJSON_IsTrue(power) ? "ON" : "OFF");
        any = true;
    }
    if (cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(b, "toggle"))) {
        cJSON_AddStringToObject(o, "state", "TOGGLE");
        any = true;
    }
    if (api::get_number(b, "brightness", v)) {
        cJSON_AddNumberToObject(o, "brightness", v);
        any = true;
    }
    double r = 0, g = 0, bl = 0;
    if (api::get_number(b, "red", r) | api::get_number(b, "green", g) | api::get_number(b, "blue", bl)) {
        cJSON *c = cJSON_AddObjectToObject(o, "color");
        cJSON_AddNumberToObject(c, "r", r);
        cJSON_AddNumberToObject(c, "g", g);
        cJSON_AddNumberToObject(c, "b", bl);
        any = true;
    }
    if (api::get_number(b, "white", v)) {
        cJSON_AddNumberToObject(o, "white", v);
        any = true;
    }
    if (api::get_number(b, "colorTemperature", v)) {
        cJSON_AddNumberToObject(o, "color_temp", v);
        any = true;
    }
    if (api::get_number(b, "transitionMs", v)) cJSON_AddNumberToObject(o, "transition", v / 1000.0);
    const std::string effect = api::str_or(b, "effect", "");
    if (!effect.empty()) {
        cJSON_AddStringToObject(o, "effect", effect.c_str());
        any = true;
    }
    if (!any) {
        cJSON_Delete(o);
        error = "command sets no fields";
        return nullptr;
    }
    return o;
}

} // namespace

void register_controller_routes(AppContext &ctx)
{
    WebServer &web = *ctx.web;

    web.route(HTTP_GET, "/controllers", RouteAuth::kUser, [&ctx](ApiRequest &req) { return send_data(req, controllers_json(*ctx.espnow)); });

    web.route(HTTP_POST, "/controllers/{id}/lights/{light}/command", RouteAuth::kUser, [&ctx](ApiRequest &req) {
        std::string error;
        cJSON *ha = to_ha(req.json(), error);
        if (ha == nullptr) return send_error(req, 400, "INVALID_COMMAND", error);
        const bool ok = ctx.espnow->send_command(req.params[0], req.params[1], ha, error);
        cJSON_Delete(ha);
        if (!ok) return send_error(req, 409, "CONTROLLER_UNAVAILABLE", error);
        cJSON *o = cJSON_CreateObject();
        cJSON_AddBoolToObject(o, "sent", true);
        return send_data(req, o, 202);
    });

    web.route(HTTP_POST, "/controllers/scenes/{scene}/activate", RouteAuth::kUser, [&ctx](ApiRequest &req) {
        const std::string id = req.params[0];
        bool local = false;
        {
            ModelLock lock(model_mutex());
            local = ctx.scenes->find(id) != nullptr && ctx.scenes->activate_scene(id, 0, "rest") == SceneResult::kOk;
        }
        const bool sent = ctx.espnow->running() && ctx.espnow->send_scene(id);
        cJSON *o = cJSON_CreateObject();
        cJSON_AddBoolToObject(o, "local", local);
        cJSON_AddBoolToObject(o, "broadcast", sent);
        return send_data(req, o, 202);
    });

    web.route(HTTP_PUT, "/integrations/espnow", RouteAuth::kUser, [&ctx](ApiRequest &req) {
        cJSON *b = req.json();
        EspNowSettings s = ctx.espnow->settings();
        api::get_bool(b, "enabled", s.enabled);
        api::get_string(b, "networkId", s.network_id, 33);
        std::string key;
        const bool has_key = api::get_string(b, "key", key, 65);
        std::string error;
        if (ctx.espnow->update(s, has_key ? &key : nullptr, error) != ESP_OK) return send_error(req, 400, "INVALID_CONFIGURATION", error);
        return send_data(req, controllers_json(*ctx.espnow));
    });

    web.route(HTTP_GET, "/integrations/espnow/key", RouteAuth::kUser, [&ctx](ApiRequest &req) {
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "key", ctx.espnow->key().c_str());
        return send_data(req, o);
    });

    web.route(HTTP_POST, "/integrations/espnow/key/regenerate", RouteAuth::kUser, [&ctx](ApiRequest &req) {
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "key", ctx.espnow->regenerate_key().c_str());
        return send_data(req, o);
    });
}

} // namespace lumen
