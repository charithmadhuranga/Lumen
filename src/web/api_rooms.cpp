// Lumen API — rooms and groups (SPEC §9.1–9.2, §17.2).
//
//   GET/POST          /rooms              GET/PUT/DELETE /rooms/{id}
//   POST /rooms/{id}/power      {power, transitionMs?}
//   POST /rooms/{id}/brightness {brightness, transitionMs?}
//   GET/POST          /groups             GET/PUT/DELETE /groups/{id}
//   POST /groups/{id}/command   light command fanned out to members
//   POST /groups/{id}/power | /brightness
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

std::string slugify(const std::string &name)
{
    std::string out;
    for (char c : name) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
            out.push_back(c);
        } else if (!out.empty() && out.back() != '_') {
            out.push_back('_');
        }
        if (out.size() >= 32) break;
    }
    while (!out.empty() && out.back() == '_') out.pop_back();
    return out.empty() ? "item" : out;
}

template <typename Exists>
std::string unique_id(const std::string &base, Exists exists)
{
    std::string id = base;
    for (int n = 2; exists(id) && n < 1000; ++n) {
        id = base + "_" + std::to_string(n);
    }
    return id;
}

struct Aggregate
{
    int total = 0;
    int on = 0;
    int brightness = 0;
};

Aggregate aggregate(const AppContext &ctx, const std::vector<std::string> &ids,
                    AggregationPolicy policy)
{
    Aggregate a;
    int sum = 0, mx = 0, mn = 100;
    for (const auto &id : ids) {
        const Channel *ch = ctx.lighting->channel_manager().find(id);
        if (ch == nullptr) continue;
        a.total++;
        if (ch->requested.power) {
            a.on++;
            const int b = ch->type == LightType::kSwitch ? 100 : ch->requested.brightness;
            sum += b;
            mx = std::max(mx, b);
            mn = std::min(mn, b);
        }
    }
    if (a.on > 0) {
        a.brightness = policy == AggregationPolicy::kMax   ? mx
                       : policy == AggregationPolicy::kMin ? mn
                                                           : sum / a.on;
    }
    return a;
}

cJSON *aggregate_json(const Aggregate &a)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddBoolToObject(o, "on", a.on > 0);
    cJSON_AddNumberToObject(o, "onCount", a.on);
    cJSON_AddNumberToObject(o, "total", a.total);
    cJSON_AddNumberToObject(o, "brightness", a.brightness);
    return o;
}

cJSON *room_json(const AppContext &ctx, const Room &r)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "id", r.id.c_str());
    cJSON_AddStringToObject(o, "name", r.name.c_str());
    cJSON_AddStringToObject(o, "floor", r.floor.c_str());
    cJSON_AddStringToObject(o, "icon", r.icon.c_str());
    cJSON_AddStringToObject(o, "floorplanId", r.floorplan_id.c_str());
    cJSON_AddItemToObject(o, "channelIds", api::string_array(r.channel_ids));
    cJSON_AddItemToObject(o, "groupIds", api::string_array(r.group_ids));
    cJSON_AddItemToObject(o, "state", aggregate_json(aggregate(ctx, r.channel_ids, AggregationPolicy::kAverage)));
    return o;
}

const char *policy_name(AggregationPolicy p)
{
    return p == AggregationPolicy::kMax ? "max" : p == AggregationPolicy::kMin ? "min" : "average";
}

bool policy_from(const std::string &s, AggregationPolicy &out)
{
    if (s == "average") out = AggregationPolicy::kAverage;
    else if (s == "max") out = AggregationPolicy::kMax;
    else if (s == "min") out = AggregationPolicy::kMin;
    else return false;
    return true;
}

cJSON *group_json(const AppContext &ctx, const Group &g)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "id", g.id.c_str());
    cJSON_AddStringToObject(o, "name", g.name.c_str());
    cJSON_AddStringToObject(o, "roomId", g.room_id.c_str());
    cJSON_AddItemToObject(o, "channelIds", api::string_array(g.channel_ids));
    cJSON_AddStringToObject(o, "policy", policy_name(g.policy));
    cJSON_AddItemToObject(o, "state", aggregate_json(aggregate(ctx, g.channel_ids, g.policy)));
    return o;
}

bool lights_exist(const AppContext &ctx, const std::vector<std::string> &ids, std::string &missing)
{
    for (const auto &id : ids) {
        if (ctx.lighting->channel_manager().find(id) == nullptr) {
            missing = id;
            return false;
        }
    }
    return true;
}

// Fan a command out to a set of lights, dropping per-light unsupported
// fields (a group "set colour" leaves dimmers alone — SPEC §9.2).
int fan_out(AppContext &ctx, const std::vector<std::string> &ids, const LightCommand &base)
{
    int queued = 0;
    for (const auto &id : ids) {
        const Channel *ch = ctx.lighting->channel_manager().find(id);
        if (ch == nullptr) continue;
        LightCommandEnvelope env;
        env.command = base;
        env.command.channel_id = id;
        const bool rgb = ch->type == LightType::kRgb || ch->type == LightType::kRgbw ||
                         ch->type == LightType::kAddressableRgb || ch->type == LightType::kAddressableRgbw;
        if (!rgb) env.command.fields &= static_cast<uint8_t>(~kFieldColor);
        if (ch->type != LightType::kRgbw && ch->type != LightType::kAddressableRgbw)
            env.command.fields &= static_cast<uint8_t>(~kFieldWhite);
        if (ch->type != LightType::kCct) env.command.fields &= static_cast<uint8_t>(~kFieldCct);
        if (ch->type == LightType::kSwitch) env.command.effect.clear();
        if (env.command.fields == 0 && env.command.effect.empty()) {
            continue; // nothing applicable to this member
        }
        env.posted_ms = static_cast<uint32_t>(esp_timer_get_time() / 1000);
        if (ctx.lighting->enqueue(env) == EnqueueResult::kOk) queued++;
    }
    return queued;
}

esp_err_t power_or_brightness(AppContext &ctx, ApiRequest &req, const std::vector<std::string> &ids,
                              bool brightness, const char *origin)
{
    cJSON *body = req.json();
    LightCommand cmd;
    cmd.origin = origin;
    double t = 0;
    if (api::get_number(body, "transitionMs", t) && t >= 0 && t <= 600000) {
        cmd.transition_ms = static_cast<uint32_t>(t);
    } else {
        cmd.use_default_transition = true;
    }
    if (brightness) {
        double b = 0;
        if (!api::get_number(body, "brightness", b) || b < 0 || b > 100) {
            return send_error(req, 400, "INVALID_REQUEST", "brightness 0..100 required");
        }
        cmd.state.brightness = static_cast<uint8_t>(b);
        cmd.fields = kFieldBrightness;
    } else {
        bool on = false;
        if (!api::get_bool(body, "power", on)) {
            return send_error(req, 400, "INVALID_REQUEST", "power (boolean) required");
        }
        cmd.state.power = on;
        cmd.fields = kFieldPower;
    }
    LightCommand plain = cmd;
    const int n = fan_out(ctx, ids, plain);
    cJSON *o = cJSON_CreateObject();
    cJSON_AddBoolToObject(o, "accepted", true);
    cJSON_AddNumberToObject(o, "lights", n);
    return send_data(req, o, 202);
}

} // namespace

void register_room_routes(AppContext &ctx)
{
    WebServer &web = *ctx.web;

    // ------------------------------------------------------------ rooms
    web.route(HTTP_GET, "/rooms", RouteAuth::kUser, [&ctx](ApiRequest &req) {
        ModelLock lock(model_mutex());
        cJSON *arr = cJSON_CreateArray();
        for (std::size_t i = 0; i < ctx.rooms->room_count(); ++i) {
            cJSON_AddItemToArray(arr, room_json(ctx, *ctx.rooms->at(i)));
        }
        return send_data(req, arr);
    });

    web.route(HTTP_GET, "/rooms/{id}", RouteAuth::kUser, [&ctx](ApiRequest &req) {
        ModelLock lock(model_mutex());
        const Room *r = ctx.rooms->find(req.params[0]);
        return r ? send_data(req, room_json(ctx, *r)) : send_error(req, 404, "NOT_FOUND", "Room not found");
    });

    web.route(HTTP_POST, "/rooms", RouteAuth::kUser, [&ctx](ApiRequest &req) {
        cJSON *body = req.json();
        Room room;
        if (!api::get_string(body, "name", room.name, 64) || room.name.empty()) {
            return send_error(req, 400, "INVALID_REQUEST", "name is required");
        }
        ModelLock lock(model_mutex());
        std::string id = api::str_or(body, "id", "");
        if (id.empty()) {
            id = unique_id(slugify(room.name), [&](const std::string &c) { return ctx.rooms->find(c) != nullptr; });
        }
        if (!light_store::valid_id(id)) return send_error(req, 400, "INVALID_REQUEST", "invalid id");
        room.id = id;
        room.floor = api::str_or(body, "floor", "").substr(0, 32);
        room.icon = api::str_or(body, "icon", "").substr(0, 32);
        room.floorplan_id = api::str_or(body, "floorplanId", "").substr(0, 48);
        api::get_string_array(body, "channelIds", room.channel_ids);
        std::string missing;
        if (!lights_exist(ctx, room.channel_ids, missing)) {
            return send_error(req, 400, "INVALID_CONFIGURATION", "Unknown light '" + missing + "'");
        }
        const RoomResult rr = ctx.rooms->register_room(room);
        if (rr == RoomResult::kDuplicate) return send_error(req, 409, "CONFLICT", "Room id already exists");
        if (rr == RoomResult::kCapacity) return send_error(req, 409, "CAPACITY", "Room limit reached");
        if (rr != RoomResult::kOk) return send_error(req, 400, "INVALID_CONFIGURATION", "Room rejected");
        // A light belongs to at most one room.
        bool lights_dirty = false;
        for (const auto &cid : room.channel_ids) {
            for (std::size_t i = 0; i < ctx.rooms->room_count(); ++i) {
                Room *other = ctx.rooms->at(i);
                if (other->id == room.id) continue;
                auto &v = other->channel_ids;
                if (std::find(v.begin(), v.end(), cid) != v.end()) {
                    v.erase(std::remove(v.begin(), v.end(), cid), v.end());
                }
            }
            Channel *ch = ctx.lighting->channel_manager().find_mutable(cid);
            if (ch != nullptr && ch->room_id != room.id) {
                ch->room_id = room.id;
                lights_dirty = true;
            }
        }
        ctx.rooms->persist();
        if (lights_dirty) ctx.save_lights();
        return send_data(req, room_json(ctx, *ctx.rooms->find(room.id)), 201);
    });

    web.route(HTTP_PUT, "/rooms/{id}", RouteAuth::kUser, [&ctx](ApiRequest &req) {
        cJSON *body = req.json();
        ModelLock lock(model_mutex());
        Room *r = ctx.rooms->find_mutable(req.params[0]);
        if (r == nullptr) return send_error(req, 404, "NOT_FOUND", "Room not found");
        const Room backup = *r;
        std::string s;
        if (api::get_string(body, "name", s, 64)) {
            if (s.empty()) return send_error(req, 400, "INVALID_REQUEST", "name cannot be empty");
            r->name = s;
        }
        if (api::get_string(body, "floor", s, 32)) r->floor = s;
        if (api::get_string(body, "icon", s, 32)) r->icon = s;
        if (api::get_string(body, "floorplanId", s, 48)) r->floorplan_id = s;
        std::vector<std::string> ids;
        bool lights_dirty = false;
        if (api::get_string_array(body, "channelIds", ids)) {
            std::string missing;
            if (!lights_exist(ctx, ids, missing)) {
                *r = backup;
                return send_error(req, 400, "INVALID_CONFIGURATION", "Unknown light '" + missing + "'");
            }
            for (const auto &old : r->channel_ids) {
                if (std::find(ids.begin(), ids.end(), old) == ids.end()) {
                    Channel *ch = ctx.lighting->channel_manager().find_mutable(old);
                    if (ch && ch->room_id == r->id) { ch->room_id.clear(); lights_dirty = true; }
                }
            }
            for (const auto &cid : ids) {
                for (std::size_t i = 0; i < ctx.rooms->room_count(); ++i) {
                    Room *other = ctx.rooms->at(i);
                    if (other->id == r->id) continue;
                    auto &v = other->channel_ids;
                    v.erase(std::remove(v.begin(), v.end(), cid), v.end());
                }
                Channel *ch = ctx.lighting->channel_manager().find_mutable(cid);
                if (ch && ch->room_id != r->id) { ch->room_id = r->id; lights_dirty = true; }
            }
            r->channel_ids = ids;
        }
        if (ctx.rooms->persist() != ConfigStatus::kOk) {
            *r = backup;
            return send_error(req, 500, "STORAGE_ERROR", "Could not save the room");
        }
        if (lights_dirty) ctx.save_lights();
        return send_data(req, room_json(ctx, *r));
    });

    web.route(HTTP_DELETE, "/rooms/{id}", RouteAuth::kUser, [&ctx](ApiRequest &req) {
        ModelLock lock(model_mutex());
        const std::string id = req.params[0];
        if (ctx.rooms->find(id) == nullptr) return send_error(req, 404, "NOT_FOUND", "Room not found");
        if (ctx.rooms->remove_room(id) != RoomResult::kOk) {
            return send_error(req, 500, "STORAGE_ERROR", "Could not delete the room");
        }
        bool dirty = false;
        ChannelManager &cm = ctx.lighting->channel_manager();
        for (std::size_t i = 0; i < cm.channel_count(); ++i) {
            if (cm.at(i).room_id == id) { cm.at(i).room_id.clear(); dirty = true; }
        }
        if (dirty) ctx.save_lights();
        for (std::size_t i = 0; i < ctx.groups->group_count(); ++i) {
            if (ctx.groups->at(i)->room_id == id) ctx.groups->at(i)->room_id.clear();
        }
        ctx.groups->persist();
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "id", id.c_str());
        return send_data(req, o);
    });

    web.route(HTTP_POST, "/rooms/{id}/power", RouteAuth::kUser, [&ctx](ApiRequest &req) {
        ModelLock lock(model_mutex());
        const Room *r = ctx.rooms->find(req.params[0]);
        if (r == nullptr) return send_error(req, 404, "NOT_FOUND", "Room not found");
        return power_or_brightness(ctx, req, r->channel_ids, false, "room");
    });

    web.route(HTTP_POST, "/rooms/{id}/brightness", RouteAuth::kUser, [&ctx](ApiRequest &req) {
        ModelLock lock(model_mutex());
        const Room *r = ctx.rooms->find(req.params[0]);
        if (r == nullptr) return send_error(req, 404, "NOT_FOUND", "Room not found");
        return power_or_brightness(ctx, req, r->channel_ids, true, "room");
    });

    // ----------------------------------------------------------- groups
    web.route(HTTP_GET, "/groups", RouteAuth::kUser, [&ctx](ApiRequest &req) {
        ModelLock lock(model_mutex());
        cJSON *arr = cJSON_CreateArray();
        for (std::size_t i = 0; i < ctx.groups->group_count(); ++i) {
            cJSON_AddItemToArray(arr, group_json(ctx, *ctx.groups->at(i)));
        }
        return send_data(req, arr);
    });

    web.route(HTTP_GET, "/groups/{id}", RouteAuth::kUser, [&ctx](ApiRequest &req) {
        ModelLock lock(model_mutex());
        const Group *g = ctx.groups->find(req.params[0]);
        return g ? send_data(req, group_json(ctx, *g)) : send_error(req, 404, "NOT_FOUND", "Group not found");
    });

    web.route(HTTP_POST, "/groups", RouteAuth::kUser, [&ctx](ApiRequest &req) {
        cJSON *body = req.json();
        Group g;
        if (!api::get_string(body, "name", g.name, 64) || g.name.empty()) {
            return send_error(req, 400, "INVALID_REQUEST", "name is required");
        }
        ModelLock lock(model_mutex());
        std::string id = api::str_or(body, "id", "");
        if (id.empty()) {
            id = unique_id(slugify(g.name), [&](const std::string &c) { return ctx.groups->find(c) != nullptr; });
        }
        if (!light_store::valid_id(id)) return send_error(req, 400, "INVALID_REQUEST", "invalid id");
        g.id = id;
        g.room_id = api::str_or(body, "roomId", "");
        if (!g.room_id.empty() && ctx.rooms->find(g.room_id) == nullptr) {
            return send_error(req, 400, "INVALID_CONFIGURATION", "Unknown room '" + g.room_id + "'");
        }
        api::get_string_array(body, "channelIds", g.channel_ids);
        std::string missing;
        if (!lights_exist(ctx, g.channel_ids, missing)) {
            return send_error(req, 400, "INVALID_CONFIGURATION", "Unknown light '" + missing + "'");
        }
        std::string policy;
        if (api::get_string(body, "policy", policy, 16) && !policy_from(policy, g.policy)) {
            return send_error(req, 400, "INVALID_REQUEST", "policy must be average, max or min");
        }
        const GroupResult gr = ctx.groups->register_group(g);
        if (gr == GroupResult::kDuplicate) return send_error(req, 409, "CONFLICT", "Group id already exists");
        if (gr == GroupResult::kCapacity) return send_error(req, 409, "CAPACITY", "Group limit reached");
        if (gr != GroupResult::kOk) return send_error(req, 400, "INVALID_CONFIGURATION", "Group rejected");
        return send_data(req, group_json(ctx, *ctx.groups->find(g.id)), 201);
    });

    web.route(HTTP_PUT, "/groups/{id}", RouteAuth::kUser, [&ctx](ApiRequest &req) {
        cJSON *body = req.json();
        ModelLock lock(model_mutex());
        Group *g = ctx.groups->find_mutable(req.params[0]);
        if (g == nullptr) return send_error(req, 404, "NOT_FOUND", "Group not found");
        const Group backup = *g;
        std::string s;
        if (api::get_string(body, "name", s, 64)) {
            if (s.empty()) return send_error(req, 400, "INVALID_REQUEST", "name cannot be empty");
            g->name = s;
        }
        if (api::get_string(body, "roomId", s, 48)) {
            if (!s.empty() && ctx.rooms->find(s) == nullptr) {
                *g = backup;
                return send_error(req, 400, "INVALID_CONFIGURATION", "Unknown room '" + s + "'");
            }
            g->room_id = s;
        }
        if (api::get_string(body, "policy", s, 16) && !policy_from(s, g->policy)) {
            *g = backup;
            return send_error(req, 400, "INVALID_REQUEST", "policy must be average, max or min");
        }
        std::vector<std::string> ids;
        if (api::get_string_array(body, "channelIds", ids)) {
            std::string missing;
            if (!lights_exist(ctx, ids, missing)) {
                *g = backup;
                return send_error(req, 400, "INVALID_CONFIGURATION", "Unknown light '" + missing + "'");
            }
            g->channel_ids = ids;
        }
        if (ctx.groups->persist() != ConfigStatus::kOk) {
            *g = backup;
            return send_error(req, 500, "STORAGE_ERROR", "Could not save the group");
        }
        return send_data(req, group_json(ctx, *g));
    });

    web.route(HTTP_DELETE, "/groups/{id}", RouteAuth::kUser, [&ctx](ApiRequest &req) {
        ModelLock lock(model_mutex());
        const std::string id = req.params[0];
        const GroupResult gr = ctx.groups->remove_group(id);
        if (gr == GroupResult::kNotFound) return send_error(req, 404, "NOT_FOUND", "Group not found");
        if (gr != GroupResult::kOk) return send_error(req, 500, "STORAGE_ERROR", "Could not delete");
        for (std::size_t i = 0; i < ctx.rooms->room_count(); ++i) {
            auto &v = ctx.rooms->at(i)->group_ids;
            v.erase(std::remove(v.begin(), v.end(), id), v.end());
        }
        ctx.rooms->persist();
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "id", id.c_str());
        return send_data(req, o);
    });

    web.route(HTTP_POST, "/groups/{id}/command", RouteAuth::kUser, [&ctx](ApiRequest &req) {
        LightCommand cmd;
        std::string error;
        cmd.origin = "group";
        if (!light_store::command_from_json(req.json(), cmd, error)) {
            return send_error(req, 400, "INVALID_COMMAND", error);
        }
        ModelLock lock(model_mutex());
        const Group *g = ctx.groups->find(req.params[0]);
        if (g == nullptr) return send_error(req, 404, "NOT_FOUND", "Group not found");
        const int n = fan_out(ctx, g->channel_ids, cmd);
        cJSON *o = cJSON_CreateObject();
        cJSON_AddBoolToObject(o, "accepted", true);
        cJSON_AddNumberToObject(o, "lights", n);
        ctx.ws->broadcast("group_state_changed", "{\"groupId\":\"" + g->id + "\"}");
        AutoEvent ae;
        ae.type = AutoEventType::kGroup;
        ae.id = g->id;
        ae.origin = "rest";
        ctx.automation->post(ae);
        return send_data(req, o, 202);
    });

    web.route(HTTP_POST, "/groups/{id}/power", RouteAuth::kUser, [&ctx](ApiRequest &req) {
        ModelLock lock(model_mutex());
        const Group *g = ctx.groups->find(req.params[0]);
        if (g == nullptr) return send_error(req, 404, "NOT_FOUND", "Group not found");
        return power_or_brightness(ctx, req, g->channel_ids, false, "group");
    });

    web.route(HTTP_POST, "/groups/{id}/brightness", RouteAuth::kUser, [&ctx](ApiRequest &req) {
        ModelLock lock(model_mutex());
        const Group *g = ctx.groups->find(req.params[0]);
        if (g == nullptr) return send_error(req, 404, "NOT_FOUND", "Group not found");
        return power_or_brightness(ctx, req, g->channel_ids, true, "group");
    });
}

} // namespace lumen
