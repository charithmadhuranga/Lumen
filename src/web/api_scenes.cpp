// Lumen API — scenes (SPEC §9.3, §17.2).
//
//   GET/POST  /scenes                GET/PUT/DELETE /scenes/{id}
//   POST /scenes/{id}/activate       {transitionMs?}
//   POST /scenes/{id}/duplicate
//   POST /scenes/capture             {name, id?, lightIds?, transitionMs?}
#include <algorithm>

#include "lumen/app/app_context.hpp"
#include "lumen/core/model_lock.hpp"
#include "lumen/lighting/light_store.h"
#include "lumen/web/api_util.hpp"

namespace lumen
{

namespace
{

std::string slug(const std::string &name)
{
    std::string out;
    for (char c : name) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) out.push_back(c);
        else if (!out.empty() && out.back() != '_') out.push_back('_');
        if (out.size() >= 32) break;
    }
    while (!out.empty() && out.back() == '_') out.pop_back();
    return out.empty() ? "scene" : out;
}

std::string unique_scene_id(const SceneManager &scenes, const std::string &base)
{
    std::string id = base;
    for (int n = 2; scenes.find(id) != nullptr && n < 1000; ++n) id = base + "_" + std::to_string(n);
    return id;
}

// Keep only the properties a light type supports (AGENTS.md §9).
void sanitize_for(const Channel &ch, SceneLightState &st)
{
    const bool rgb = ch.type == LightType::kRgb || ch.type == LightType::kRgbw ||
                     ch.type == LightType::kAddressableRgb || ch.type == LightType::kAddressableRgbw;
    if (!rgb) st.red = st.green = st.blue = 0;
    if (ch.type != LightType::kRgbw && ch.type != LightType::kAddressableRgbw) st.white = 0;
    if (ch.type != LightType::kCct) st.color_temperature = 0;
    if (ch.type == LightType::kSwitch) st.brightness = st.power ? 100 : 0;
}

cJSON *scene_json(const Scene &s)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "id", s.id.c_str());
    cJSON_AddStringToObject(o, "name", s.name.c_str());
    cJSON_AddNumberToObject(o, "transitionMs", s.transition_ms);
    cJSON *arr = cJSON_AddArrayToObject(o, "states");
    for (const auto &st : s.states) {
        cJSON *e = cJSON_CreateObject();
        cJSON_AddStringToObject(e, "lightId", st.light_id.c_str());
        cJSON_AddBoolToObject(e, "power", st.power);
        cJSON_AddNumberToObject(e, "brightness", st.brightness);
        cJSON_AddNumberToObject(e, "red", st.red);
        cJSON_AddNumberToObject(e, "green", st.green);
        cJSON_AddNumberToObject(e, "blue", st.blue);
        cJSON_AddNumberToObject(e, "white", st.white);
        cJSON_AddNumberToObject(e, "colorTemperature", st.color_temperature);
        cJSON_AddItemToArray(arr, e);
    }
    return o;
}

bool parse_states(const AppContext &ctx, const cJSON *body, std::vector<SceneLightState> &out,
                  std::string &error)
{
    const cJSON *arr = api::field(body, "states");
    if (!cJSON_IsArray(arr)) {
        error = "states must be an array";
        return false;
    }
    out.clear();
    const cJSON *it = nullptr;
    cJSON_ArrayForEach(it, arr)
    {
        if (out.size() >= ChannelManager::kMaxChannels) {
            error = "too many states";
            return false;
        }
        SceneLightState st;
        if (!api::get_string(it, "lightId", st.light_id, 48)) {
            error = "each state needs lightId";
            return false;
        }
        const Channel *ch = ctx.lighting->channel_manager().find(st.light_id);
        if (ch == nullptr) {
            error = "Unknown light '" + st.light_id + "'";
            return false;
        }
        for (const auto &prev : out) {
            if (prev.light_id == st.light_id) {
                error = "light '" + st.light_id + "' listed twice";
                return false;
            }
        }
        api::get_bool(it, "power", st.power);
        double v = 0;
        auto num = [&](const char *key, int lo, int hi, int &dst) -> bool {
            if (!api::has(it, key)) return true;
            if (!api::get_number(it, key, v) || v < lo || v > hi) {
                error = std::string(key) + " out of range";
                return false;
            }
            dst = static_cast<int>(v);
            return true;
        };
        int b = 0, r = 0, g = 0, bl = 0, w = 0, ct = 0;
        if (!num("brightness", 0, 100, b) || !num("red", 0, 255, r) || !num("green", 0, 255, g) ||
            !num("blue", 0, 255, bl) || !num("white", 0, 255, w) ||
            !num("colorTemperature", 0, 20000, ct)) {
            return false;
        }
        st.brightness = static_cast<uint8_t>(b);
        st.red = static_cast<uint8_t>(r);
        st.green = static_cast<uint8_t>(g);
        st.blue = static_cast<uint8_t>(bl);
        st.white = static_cast<uint8_t>(w);
        st.color_temperature = static_cast<uint16_t>(ct);
        sanitize_for(*ch, st);
        out.push_back(st);
    }
    return true;
}

esp_err_t scene_error(ApiRequest &req, SceneResult r)
{
    switch (r) {
    case SceneResult::kNotFound: return send_error(req, 404, "NOT_FOUND", "Scene not found");
    case SceneResult::kDuplicate: return send_error(req, 409, "CONFLICT", "Scene id already exists");
    case SceneResult::kCapacity: return send_error(req, 409, "CAPACITY", "Scene limit reached");
    default: return send_error(req, 400, "INVALID_CONFIGURATION", "Scene rejected");
    }
}

} // namespace

void register_scene_routes(AppContext &ctx)
{
    WebServer &web = *ctx.web;

    web.route(HTTP_GET, "/scenes", RouteAuth::kUser, [&ctx](ApiRequest &req) {
        ModelLock lock(model_mutex());
        cJSON *arr = cJSON_CreateArray();
        for (std::size_t i = 0; i < ctx.scenes->scene_count(); ++i) {
            cJSON_AddItemToArray(arr, scene_json(*ctx.scenes->at(i)));
        }
        return send_data(req, arr);
    });

    web.route(HTTP_GET, "/scenes/{id}", RouteAuth::kUser, [&ctx](ApiRequest &req) {
        ModelLock lock(model_mutex());
        const Scene *s = ctx.scenes->find(req.params[0]);
        return s ? send_data(req, scene_json(*s)) : send_error(req, 404, "NOT_FOUND", "Scene not found");
    });

    web.route(HTTP_POST, "/scenes", RouteAuth::kUser, [&ctx](ApiRequest &req) {
        cJSON *body = req.json();
        Scene s;
        if (!api::get_string(body, "name", s.name, 64) || s.name.empty()) {
            return send_error(req, 400, "INVALID_REQUEST", "name is required");
        }
        double t = 0;
        if (api::get_number(body, "transitionMs", t)) {
            if (t < 0 || t > 600000) return send_error(req, 400, "INVALID_REQUEST", "transitionMs 0..600000");
            s.transition_ms = static_cast<uint32_t>(t);
        }
        ModelLock lock(model_mutex());
        std::string error;
        if (!parse_states(ctx, body, s.states, error)) {
            return send_error(req, 400, "INVALID_CONFIGURATION", error);
        }
        s.id = api::str_or(body, "id", "");
        if (s.id.empty()) s.id = unique_scene_id(*ctx.scenes, slug(s.name));
        if (!light_store::valid_id(s.id)) return send_error(req, 400, "INVALID_REQUEST", "invalid id");
        const SceneResult r = ctx.scenes->register_scene(s);
        if (r != SceneResult::kOk) return scene_error(req, r);
        return send_data(req, scene_json(*ctx.scenes->find(s.id)), 201);
    });

    web.route(HTTP_POST, "/scenes/capture", RouteAuth::kUser, [&ctx](ApiRequest &req) {
        cJSON *body = req.json();
        Scene s;
        if (!api::get_string(body, "name", s.name, 64) || s.name.empty()) {
            return send_error(req, 400, "INVALID_REQUEST", "name is required");
        }
        double t = 0;
        if (api::get_number(body, "transitionMs", t) && t >= 0 && t <= 600000) {
            s.transition_ms = static_cast<uint32_t>(t);
        }
        ModelLock lock(model_mutex());
        std::vector<std::string> ids;
        const bool subset = api::get_string_array(body, "lightIds", ids);
        const ChannelManager &cm = ctx.lighting->channel_manager();
        for (std::size_t i = 0; i < cm.channel_count(); ++i) {
            const Channel &ch = cm.at(i);
            if (subset && std::find(ids.begin(), ids.end(), ch.id) == ids.end()) continue;
            SceneLightState st;
            st.light_id = ch.id;
            st.power = ch.requested.power;
            st.brightness = ch.requested.brightness;
            st.red = ch.requested.red;
            st.green = ch.requested.green;
            st.blue = ch.requested.blue;
            st.white = ch.requested.white;
            st.color_temperature = ch.requested.color_temperature;
            sanitize_for(ch, st);
            s.states.push_back(st);
        }
        s.id = api::str_or(body, "id", "");
        if (s.id.empty()) s.id = unique_scene_id(*ctx.scenes, slug(s.name));
        if (!light_store::valid_id(s.id)) return send_error(req, 400, "INVALID_REQUEST", "invalid id");
        const SceneResult r = ctx.scenes->register_scene(s);
        if (r != SceneResult::kOk) return scene_error(req, r);
        return send_data(req, scene_json(*ctx.scenes->find(s.id)), 201);
    });

    web.route(HTTP_PUT, "/scenes/{id}", RouteAuth::kUser, [&ctx](ApiRequest &req) {
        cJSON *body = req.json();
        ModelLock lock(model_mutex());
        Scene *s = ctx.scenes->find_mutable(req.params[0]);
        if (s == nullptr) return send_error(req, 404, "NOT_FOUND", "Scene not found");
        const Scene backup = *s;
        std::string name;
        if (api::get_string(body, "name", name, 64)) {
            if (name.empty()) return send_error(req, 400, "INVALID_REQUEST", "name cannot be empty");
            s->name = name;
        }
        double t = 0;
        if (api::get_number(body, "transitionMs", t)) {
            if (t < 0 || t > 600000) {
                *s = backup;
                return send_error(req, 400, "INVALID_REQUEST", "transitionMs 0..600000");
            }
            s->transition_ms = static_cast<uint32_t>(t);
        }
        if (api::has(body, "states")) {
            std::string error;
            std::vector<SceneLightState> states;
            if (!parse_states(ctx, body, states, error)) {
                *s = backup;
                return send_error(req, 400, "INVALID_CONFIGURATION", error);
            }
            s->states = states;
        }
        if (ctx.scenes->persist() != ConfigStatus::kOk) {
            *s = backup;
            return send_error(req, 500, "STORAGE_ERROR", "Could not save the scene");
        }
        return send_data(req, scene_json(*s));
    });

    web.route(HTTP_DELETE, "/scenes/{id}", RouteAuth::kUser, [&ctx](ApiRequest &req) {
        ModelLock lock(model_mutex());
        const SceneResult r = ctx.scenes->delete_scene(req.params[0]);
        if (r != SceneResult::kOk) return scene_error(req, r);
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "id", req.params[0].c_str());
        return send_data(req, o);
    });

    web.route(HTTP_POST, "/scenes/{id}/activate", RouteAuth::kUser, [&ctx](ApiRequest &req) {
        double t = 0;
        uint32_t override_ms = 0;
        if (api::get_number(req.json(), "transitionMs", t) && t >= 1 && t <= 600000) {
            override_ms = static_cast<uint32_t>(t);
        }
        std::string command_id = api::str_or(req.json(), "commandId", "");
        ModelLock lock(model_mutex());
        const SceneResult r = ctx.scenes->activate_scene(req.params[0], override_ms, "rest", command_id);
        if (r != SceneResult::kOk) return scene_error(req, r);
        ctx.ws->broadcast("scene_activated", "{\"sceneId\":\"" + req.params[0] + "\"}", false);
        cJSON *o = cJSON_CreateObject();
        cJSON_AddBoolToObject(o, "accepted", true);
        return send_data(req, o, 202);
    });

    web.route(HTTP_POST, "/scenes/{id}/duplicate", RouteAuth::kUser, [&ctx](ApiRequest &req) {
        ModelLock lock(model_mutex());
        const Scene *src = ctx.scenes->find(req.params[0]);
        if (src == nullptr) return send_error(req, 404, "NOT_FOUND", "Scene not found");
        Scene copy = *src;
        copy.name = (src->name + " copy").substr(0, 64);
        copy.id = unique_scene_id(*ctx.scenes, slug(copy.name));
        const SceneResult r = ctx.scenes->register_scene(copy);
        if (r != SceneResult::kOk) return scene_error(req, r);
        return send_data(req, scene_json(*ctx.scenes->find(copy.id)), 201);
    });
}

} // namespace lumen
