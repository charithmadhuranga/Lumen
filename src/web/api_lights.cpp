// Lumen API — lights (SPEC §8, §17.2).
//
//   GET    /lights                    all lights with state
//   POST   /lights                    create (definition)
//   GET    /lights/{id}
//   PUT    /lights/{id}               update definition (partial merge)
//   DELETE /lights/{id}               also detaches it from rooms/groups/scenes
//   POST   /lights/{id}/command       {power|brightness|red..|colorTemperature|
//                                      toggle|effect|transitionMs}
//   GET    /lights/effects            effect catalogue
//
// Commands only enqueue into the lighting engine (AGENTS.md §4.7); the
// resulting state arrives over the WebSocket.
#include <algorithm>

#include "esp_timer.h"
#include "lumen/app/app_context.hpp"
#include "lumen/core/model_lock.hpp"
#include "lumen/lighting/light_store.h"
#include "lumen/web/api_util.hpp"

namespace lumen
{

namespace
{

cJSON *light_json(const AppContext &ctx, const Channel &ch)
{
    cJSON *o = light_store::channel_to_json(ch, true);
    cJSON_AddBoolToObject(o, "available", ctx.lighting->hardware_ok(ch.id));
    const std::string effect = ctx.lighting->active_effect(ch.id);
    if (!effect.empty()) {
        cJSON_AddStringToObject(o, "effect", effect.c_str());
    }
    return o;
}

// Supported controls per type — the UI renders only these (AGENTS.md §9).
cJSON *capabilities(LightType type)
{
    cJSON *c = cJSON_CreateObject();
    const bool rgb = type == LightType::kRgb || type == LightType::kRgbw ||
                     type == LightType::kAddressableRgb || type == LightType::kAddressableRgbw;
    cJSON_AddBoolToObject(c, "brightness", type != LightType::kSwitch);
    cJSON_AddBoolToObject(c, "color", rgb);
    cJSON_AddBoolToObject(c, "white", type == LightType::kRgbw || type == LightType::kAddressableRgbw);
    cJSON_AddBoolToObject(c, "colorTemperature", type == LightType::kCct);
    cJSON_AddBoolToObject(c, "effects", type != LightType::kSwitch);
    return c;
}

uint32_t now_ms()
{
    return static_cast<uint32_t>(esp_timer_get_time() / 1000);
}

// Strip a deleted light id from every room/group/scene that references it.
void detach_everywhere(AppContext &ctx, const std::string &id)
{
    bool rooms_dirty = false, groups_dirty = false, scenes_dirty = false;
    for (std::size_t i = 0; i < ctx.rooms->room_count(); ++i) {
        Room *r = ctx.rooms->at(i);
        auto &v = r->channel_ids;
        const auto before = v.size();
        v.erase(std::remove(v.begin(), v.end(), id), v.end());
        rooms_dirty |= v.size() != before;
    }
    for (std::size_t i = 0; i < ctx.groups->group_count(); ++i) {
        Group *g = ctx.groups->at(i);
        auto &v = g->channel_ids;
        const auto before = v.size();
        v.erase(std::remove(v.begin(), v.end(), id), v.end());
        groups_dirty |= v.size() != before;
    }
    for (std::size_t i = 0; i < ctx.scenes->scene_count(); ++i) {
        Scene *s = ctx.scenes->at(i);
        auto &v = s->states;
        const auto before = v.size();
        v.erase(std::remove_if(v.begin(), v.end(),
                               [&](const SceneLightState &st) { return st.light_id == id; }),
                v.end());
        scenes_dirty |= v.size() != before;
    }
    if (rooms_dirty) ctx.rooms->persist();
    if (groups_dirty) ctx.groups->persist();
    if (scenes_dirty) ctx.scenes->persist();
}

// Keep Room.channel_ids consistent with Channel.room_id.
void sync_room_membership(AppContext &ctx, const std::string &light_id, const std::string &room_id)
{
    bool dirty = false;
    for (std::size_t i = 0; i < ctx.rooms->room_count(); ++i) {
        Room *r = ctx.rooms->at(i);
        auto &v = r->channel_ids;
        const bool member = std::find(v.begin(), v.end(), light_id) != v.end();
        if (r->id == room_id && !member) {
            v.push_back(light_id);
            dirty = true;
        } else if (r->id != room_id && member) {
            v.erase(std::remove(v.begin(), v.end(), light_id), v.end());
            dirty = true;
        }
    }
    if (dirty) ctx.rooms->persist();
}

} // namespace

void register_light_routes(AppContext &ctx)
{
    WebServer &web = *ctx.web;

    web.route(HTTP_GET, "/lights", RouteAuth::kControl, [&ctx](ApiRequest &req) {
        ModelLock lock(model_mutex());
        cJSON *arr = cJSON_CreateArray();
        const ChannelManager &cm = ctx.lighting->channel_manager();
        for (std::size_t i = 0; i < cm.channel_count(); ++i) {
            cJSON *o = light_json(ctx, cm.at(i));
            cJSON_AddItemToObject(o, "capabilities", capabilities(cm.at(i).type));
            cJSON_AddItemToArray(arr, o);
        }
        return send_data(req, arr);
    });

    web.route(HTTP_GET, "/lights/effects", RouteAuth::kControl, [](ApiRequest &req) {
        static const char *names[] = {"breathing", "pulse",  "rainbow", "color_cycle",
                                      "candlelight", "sunrise", "sunset", "strobe"};
        cJSON *arr = cJSON_CreateArray();
        for (const char *n : names) cJSON_AddItemToArray(arr, cJSON_CreateString(n));
        return send_data(req, arr);
    });

    web.route(HTTP_GET, "/lights/{id}", RouteAuth::kControl, [&ctx](ApiRequest &req) {
        ModelLock lock(model_mutex());
        const Channel *ch = ctx.lighting->channel_manager().find(req.params[0]);
        if (ch == nullptr) return send_error(req, 404, "NOT_FOUND", "Light not found");
        cJSON *o = light_json(ctx, *ch);
        cJSON_AddItemToObject(o, "capabilities", capabilities(ch->type));
        return send_data(req, o);
    });

    web.route(HTTP_POST, "/lights", RouteAuth::kUser, [&ctx](ApiRequest &req) {
        Channel ch;
        std::string error;
        if (!light_store::channel_from_json(req.json(), ch, error)) {
            return send_error(req, 400, "INVALID_CONFIGURATION", error);
        }
        ModelLock lock(model_mutex());
        if (ctx.lighting->channel_manager().find(ch.id) != nullptr) {
            return send_error(req, 409, "CONFLICT", "A light with id '" + ch.id + "' already exists");
        }
        if (!ch.room_id.empty() && ctx.rooms->find(ch.room_id) == nullptr) {
            return send_error(req, 400, "INVALID_CONFIGURATION", "Unknown room '" + ch.room_id + "'");
        }
        if (!light_store::validate_against(ch, ctx.hardware->profile(),
                                           ctx.lighting->channel_manager(), "", error)) {
            return send_error(req, 400, "INVALID_CONFIGURATION", error);
        }
        if (!ctx.lighting->register_channel(ch)) {
            return send_error(req, 409, "CAPACITY", "Light limit reached (64)");
        }
        if (ctx.save_lights() != ConfigStatus::kOk) {
            ctx.lighting->remove_channel(ch.id);
            return send_error(req, 500, "STORAGE_ERROR", "Could not save the light");
        }
        sync_room_membership(ctx, ch.id, ch.room_id);
        const Channel *saved = ctx.lighting->channel_manager().find(ch.id);
        cJSON *o = light_json(ctx, *saved);
        cJSON_AddItemToObject(o, "capabilities", capabilities(saved->type));
        return send_data(req, o, 201);
    });

    web.route(HTTP_PUT, "/lights/{id}", RouteAuth::kUser, [&ctx](ApiRequest &req) {
        ModelLock lock(model_mutex());
        const Channel *current = ctx.lighting->channel_manager().find(req.params[0]);
        if (current == nullptr) return send_error(req, 404, "NOT_FOUND", "Light not found");
        cJSON *body = req.json();
        if (!cJSON_IsObject(body)) return send_error(req, 400, "INVALID_REQUEST", "JSON object expected");
        // Merge: start from the current definition, overlay provided fields.
        cJSON *merged = light_store::channel_to_json(*current, false);
        const cJSON *item = nullptr;
        cJSON_ArrayForEach(item, body)
        {
            if (item->string == nullptr || std::string(item->string) == "id") continue;
            cJSON_DeleteItemFromObjectCaseSensitive(merged, item->string);
            cJSON_AddItemToObject(merged, item->string, cJSON_Duplicate(item, 1));
        }
        Channel updated;
        std::string error;
        const bool ok = light_store::channel_from_json(merged, updated, error);
        cJSON_Delete(merged);
        if (!ok) return send_error(req, 400, "INVALID_CONFIGURATION", error);
        if (!updated.room_id.empty() && ctx.rooms->find(updated.room_id) == nullptr) {
            return send_error(req, 400, "INVALID_CONFIGURATION", "Unknown room '" + updated.room_id + "'");
        }
        if (!light_store::validate_against(updated, ctx.hardware->profile(),
                                           ctx.lighting->channel_manager(), updated.id, error)) {
            return send_error(req, 400, "INVALID_CONFIGURATION", error);
        }
        const Channel previous = *current;
        ctx.lighting->update_channel(updated);
        if (ctx.save_lights() != ConfigStatus::kOk) {
            ctx.lighting->update_channel(previous);
            return send_error(req, 500, "STORAGE_ERROR", "Could not save the light");
        }
        sync_room_membership(ctx, updated.id, updated.room_id);
        const Channel *saved = ctx.lighting->channel_manager().find(updated.id);
        cJSON *o = light_json(ctx, *saved);
        cJSON_AddItemToObject(o, "capabilities", capabilities(saved->type));
        return send_data(req, o);
    });

    web.route(HTTP_DELETE, "/lights/{id}", RouteAuth::kUser, [&ctx](ApiRequest &req) {
        ModelLock lock(model_mutex());
        const std::string id = req.params[0];
        const Channel *current = ctx.lighting->channel_manager().find(id);
        if (current == nullptr) return send_error(req, 404, "NOT_FOUND", "Light not found");
        const Channel backup = *current;
        ctx.lighting->remove_channel(id);
        if (ctx.save_lights() != ConfigStatus::kOk) {
            ctx.lighting->register_channel(backup);
            return send_error(req, 500, "STORAGE_ERROR", "Could not save");
        }
        detach_everywhere(ctx, id);
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "id", id.c_str());
        // config_changed is broadcast by the lighting listener (kConfig).
        return send_data(req, o);
    });

    web.route(HTTP_POST, "/lights/{id}/command", RouteAuth::kControl, [&ctx](ApiRequest &req) {
        LightCommandEnvelope env;
        env.command.channel_id = req.params[0];
        env.command.origin = "rest";
        std::string error;
        cJSON *body = req.json();
        bool toggle = false;
        api::get_bool(body, "toggle", toggle);
        if (!light_store::command_from_json(body, env.command, error) && !toggle) {
            return send_error(req, 400, "INVALID_COMMAND", error);
        }
        {
            ModelLock lock(model_mutex());
            const Channel *ch = ctx.lighting->channel_manager().find(env.command.channel_id);
            if (ch == nullptr) return send_error(req, 404, "NOT_FOUND", "Light not found");
            // Reject properties the light does not support (never silently).
            const bool rgb = ch->type == LightType::kRgb || ch->type == LightType::kRgbw ||
                             ch->type == LightType::kAddressableRgb ||
                             ch->type == LightType::kAddressableRgbw;
            if ((env.command.fields & kFieldColor) && !rgb) {
                return send_error(req, 422, "UNSUPPORTED_PROPERTY", "This light has no colour channels");
            }
            if ((env.command.fields & kFieldWhite) && ch->type != LightType::kRgbw &&
                ch->type != LightType::kAddressableRgbw) {
                return send_error(req, 422, "UNSUPPORTED_PROPERTY", "This light has no white channel");
            }
            if ((env.command.fields & kFieldCct) && ch->type != LightType::kCct) {
                return send_error(req, 422, "UNSUPPORTED_PROPERTY", "This light is not tunable white");
            }
            if (!env.command.effect.empty() && env.command.effect != "stop" &&
                ch->type == LightType::kSwitch) {
                return send_error(req, 422, "UNSUPPORTED_PROPERTY", "Effects need a dimmable light");
            }
            if (toggle) {
                env.command.state.power = !ch->requested.power;
                env.command.fields |= kFieldPower;
            }
        }
        env.posted_ms = now_ms();
        if (ctx.lighting->enqueue(env) != EnqueueResult::kOk) {
            return send_error(req, 503, "BUSY", "Lighting queue full - retry");
        }
        cJSON *o = cJSON_CreateObject();
        cJSON_AddBoolToObject(o, "accepted", true);
        if (!env.command.command_id.empty()) {
            cJSON_AddStringToObject(o, "commandId", env.command.command_id.c_str());
        }
        return send_data(req, o, 202);
    });
}

} // namespace lumen
