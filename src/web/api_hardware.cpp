// Lumen API — board profiles and hardware (SPEC §5–7, §17.2, DESIGN §7.8).
//
//   GET    /boards                    profile list (built-in + custom)
//   GET    /boards/active             active profile document
//   GET    /boards/{id}               profile document (+ builtIn flag)
//   GET    /boards/{id}/pins          computed pin map for the Pin Mapping grid
//   POST   /boards/validate           {profile} -> ValidationReport + pin map
//   POST   /boards                    create custom profile
//   POST   /boards/{id}/duplicate     {id?, name?} copy (built-ins are read-only)
//   PUT    /boards/{id}               update custom profile
//   DELETE /boards/{id}               delete custom profile (not the active one)
//   POST   /boards/{id}/apply         {force?} transactional apply + rollback
//   POST   /boards/apply              {id, force?} (SPEC alias)
//   GET    /hardware/status           outputs, inputs, peripheral health
//   GET    /hardware/inputs | /hardware/outputs
//   POST   /hardware/outputs/{id}/test {level 0..100}
//   GET    /hardware/i2c/scan         real bus scan (?bus=<resource id>)
#include <algorithm>
#include <cstdio>

#include "esp_heap_caps.h"
#include "esp_system.h"
#include "lumen/app/app_context.hpp"
#include "lumen/core/model_lock.hpp"
#include "lumen/hardware/board_profile_loader.h"
#include "lumen/hardware/mcu_capabilities.h"
#include "lumen/hardware/pin_validator.h"
#include "lumen/web/api_util.hpp"
#include "lumen/web/board_json.hpp"

namespace lumen
{

namespace
{

cJSON *parse_or_null(const std::string &json)
{
    return json.empty() ? nullptr : cJSON_Parse(json.c_str());
}

cJSON *report_json(const ValidationReport &report)
{
    cJSON *o = parse_or_null(web::validation_report_to_json(report));
    return o != nullptr ? o : cJSON_CreateObject();
}

esp_err_t send_validation_failure(ApiRequest &req, const ValidationReport &report)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "success", false);
    cJSON_AddItemToObject(root, "data", report_json(report));
    cJSON *err = cJSON_AddObjectToObject(root, "error");
    cJSON_AddStringToObject(err, "code", "INVALID_CONFIGURATION");
    const std::string msg = report.errors.empty() ? "Profile is invalid" : report.errors.front().message;
    cJSON_AddStringToObject(err, "message", msg.c_str());
    const std::string text = api::to_string(root);
    cJSON_Delete(root);
    return send_raw_json(req.raw, 400, text);
}

template <typename T>
bool contains(const std::vector<T> &v, int32_t x)
{
    return std::find(v.begin(), v.end(), static_cast<T>(x)) != v.end();
}

// Pin map: every GPIO of the MCU with its role, direction and conflict
// state. All GPIO knowledge stays in firmware (AGENTS.md §9).
cJSON *pin_map_json(const BoardProfile &profile, const ValidationReport *report)
{
    cJSON *arr = cJSON_CreateArray();
    const McuCapabilities *caps = McuCapabilityRegistry::find(profile.mcu);
    if (caps == nullptr) return arr;
    const std::vector<PinAssignment> uses = collect_pin_assignments(profile);
    for (uint32_t g = 0; g < caps->gpio.gpio_count; ++g) {
        const int32_t gpio = static_cast<int32_t>(g);
        std::vector<const PinAssignment *> mine;
        for (const auto &u : uses) {
            if (u.gpio == gpio) mine.push_back(&u);
        }
        const bool flash = contains(caps->gpio.flash_reserved, gpio);
        cJSON *o = cJSON_CreateObject();
        cJSON_AddNumberToObject(o, "gpio", gpio);
        std::string role = "free";
        std::string owner;
        std::string dir = contains(caps->gpio.input_only, gpio) ? "input-only" : "free";
        if (!mine.empty()) {
            role = mine.front()->role;
            owner = mine.front()->label;
            const auto res = std::find_if(profile.resources.begin(), profile.resources.end(),
                                          [&](const BoardResource &r) { return r.id == mine.front()->owner; });
            if (res != profile.resources.end() && !res->direction.empty()) dir = res->direction;
            else dir = mine.front()->outputish ? "output" : "input";
        } else if (flash) {
            role = "flash";
        }
        std::string reason;
        bool conflict = mine.size() > 1;
        if (conflict) reason = "assigned to " + mine[0]->label + " and " + mine[1]->label;
        if (report != nullptr) {
            for (const auto &e : report->errors) {
                for (const auto *u : mine) {
                    if (e.id == u->label || e.id.find(u->owner) != std::string::npos) {
                        conflict = true;
                        if (reason.empty()) reason = e.message;
                    }
                }
            }
        }
        cJSON_AddStringToObject(o, "role", role.c_str());
        cJSON_AddStringToObject(o, "owner", owner.c_str());
        cJSON_AddStringToObject(o, "dir", dir.c_str());
        cJSON_AddBoolToObject(o, "strapping", contains(caps->gpio.strapping, gpio));
        cJSON_AddBoolToObject(o, "input_only", contains(caps->gpio.input_only, gpio));
        cJSON_AddBoolToObject(o, "reserved", flash);
        cJSON_AddBoolToObject(o, "conflict", conflict);
        if (!reason.empty()) cJSON_AddStringToObject(o, "reason", reason.c_str());
        cJSON_AddItemToArray(arr, o);
    }
    return arr;
}

cJSON *profile_doc(const AppContext &ctx, const std::string &id, const std::string &json)
{
    cJSON *o = parse_or_null(json);
    if (o == nullptr) return nullptr;
    cJSON_DeleteItemFromObjectCaseSensitive(o, "builtIn");
    cJSON_AddBoolToObject(o, "builtIn", ctx.boards->is_builtin(id));
    cJSON_AddBoolToObject(o, "active", id == ctx.boards->active_id());
    return o;
}

cJSON *outputs_json(const AppContext &ctx)
{
    cJSON *arr = cJSON_CreateArray();
    const ChannelManager &cm = ctx.lighting->channel_manager();
    for (const auto &o : ctx.hardware->outputs()) {
        cJSON *e = cJSON_CreateObject();
        cJSON_AddStringToObject(e, "id", o.id.c_str());
        cJSON_AddStringToObject(e, "name", o.name.c_str());
        cJSON_AddStringToObject(e, "driver", o.driver.c_str());
        cJSON_AddStringToObject(e, "type", o.driver == "pca9685" || o.driver == "ledc" ? "pwm" : "switch");
        cJSON_AddBoolToObject(e, "state", o.on);
        cJSON_AddNumberToObject(e, "value", (o.level * 100 + 32767) / 65535);
        // Which light (if any) owns this output.
        for (std::size_t i = 0; i < cm.channel_count(); ++i) {
            const Channel &ch = cm.at(i);
            if (ch.driver.device_id != o.device) continue;
            const int first = ch.driver.type == "pca9685" ? ch.driver.channel : 0;
            const int last = first + (ch.driver.type == "pca9685" ? light_type_output_count(ch.type) - 1 : 0);
            if (o.channel >= first && o.channel <= last) {
                cJSON_AddStringToObject(e, "lightId", ch.id.c_str());
                cJSON_AddStringToObject(e, "lightName", ch.name.c_str());
            }
        }
        cJSON_AddItemToArray(arr, e);
    }
    return arr;
}

cJSON *inputs_json(const AppContext &ctx)
{
    cJSON *arr = cJSON_CreateArray();
    for (const auto &in : ctx.hardware->inputs()) {
        cJSON *e = cJSON_CreateObject();
        cJSON_AddStringToObject(e, "id", in.id.c_str());
        cJSON_AddStringToObject(e, "name", in.name.c_str());
        cJSON_AddStringToObject(e, "source", in.source.c_str());
        cJSON_AddBoolToObject(e, "state", in.state);
        cJSON_AddBoolToObject(e, "healthy", in.healthy);
        cJSON_AddNumberToObject(e, "changedMs", in.changed_ms);
        cJSON_AddItemToArray(arr, e);
    }
    return arr;
}

esp_err_t do_apply(AppContext &ctx, ApiRequest &req, const std::string &id)
{
    bool force = false;
    api::get_bool(req.json(), "force", force);
    const ProfileApplyOutcome out = ctx.apply_profile(id, force);
    cJSON *o = cJSON_CreateObject();
    cJSON_AddBoolToObject(o, "applied", out.applied);
    cJSON_AddBoolToObject(o, "rolled_back", out.rolled_back);
    cJSON_AddStringToObject(o, "active_board", out.active_board.c_str());
    cJSON_AddStringToObject(o, "message", out.message.c_str());
    cJSON_AddItemToObject(o, "report", report_json(out.report));
    if (!out.applied && !out.rolled_back && out.report.has_errors()) {
        cJSON_Delete(o);
        return send_validation_failure(req, out.report);
    }
    if (out.applied) {
        ctx.ws->broadcast("hardware_status_changed", "{\"reason\":\"profile_applied\"}", false);
    }
    return send_data(req, o, out.applied ? 200 : 409);
}

} // namespace

void register_hardware_routes(AppContext &ctx)
{
    WebServer &web = *ctx.web;

    web.route(HTTP_GET, "/boards", RouteAuth::kUser, [&ctx](ApiRequest &req) {
        cJSON *arr = cJSON_CreateArray();
        for (const auto &b : ctx.boards->list()) {
            cJSON *o = cJSON_CreateObject();
            cJSON_AddStringToObject(o, "id", b.id.c_str());
            cJSON_AddStringToObject(o, "name", b.name.c_str());
            cJSON_AddStringToObject(o, "mcu", b.mcu.c_str());
            cJSON_AddBoolToObject(o, "builtIn", b.builtin);
            cJSON_AddStringToObject(o, "desc", b.description.c_str());
            cJSON_AddBoolToObject(o, "active", b.active);
            cJSON_AddBoolToObject(o, "unverified", b.unverified);
            cJSON_AddItemToArray(arr, o);
        }
        return send_data(req, arr);
    });

    web.route(HTTP_GET, "/boards/active", RouteAuth::kUser, [&ctx](ApiRequest &req) {
        std::string json;
        const std::string id = ctx.boards->active_id();
        if (!ctx.boards->get_json(id, json)) return send_error(req, 404, "NOT_FOUND", "Active profile missing");
        cJSON *o = profile_doc(ctx, id, json);
        return o ? send_data(req, o) : send_error(req, 500, "INTERNAL_ERROR", "Profile unreadable");
    });

    web.route(HTTP_POST, "/boards/validate", RouteAuth::kUser, [&ctx](ApiRequest &req) {
        BoardProfile parsed;
        bool parse_ok = false;
        const ValidationReport report = ctx.boards->validate(req.body, parsed, parse_ok);
        cJSON *o = report_json(report);
        if (parse_ok) cJSON_AddItemToObject(o, "pins", pin_map_json(parsed, &report));
        return send_data(req, o);
    }, 32 * 1024);

    web.route(HTTP_POST, "/boards", RouteAuth::kUser, [&ctx](ApiRequest &req) {
        ValidationReport report;
        std::string id;
        const BoardOpResult r = ctx.boards->create(req.body, report, id);
        if (r == BoardOpResult::kInvalid) return send_validation_failure(req, report);
        if (r == BoardOpResult::kConflict)
            return send_error(req, 409, "CONFLICT", "A profile with this id exists (or the custom profile limit is reached)");
        if (r != BoardOpResult::kOk) return send_error(req, 500, "STORAGE_ERROR", "Could not save the profile");
        std::string json;
        ctx.boards->get_json(id, json);
        return send_data(req, profile_doc(ctx, id, json), 201);
    }, 32 * 1024);

    web.route(HTTP_POST, "/boards/apply", RouteAuth::kUser, [&ctx](ApiRequest &req) {
        std::string id;
        if (!api::get_string(req.json(), "id", id, 48)) return send_error(req, 400, "INVALID_REQUEST", "id required");
        return do_apply(ctx, req, id);
    });

    web.route(HTTP_GET, "/boards/{id}", RouteAuth::kUser, [&ctx](ApiRequest &req) {
        std::string json;
        if (!ctx.boards->get_json(req.params[0], json)) return send_error(req, 404, "NOT_FOUND", "Profile not found");
        cJSON *o = profile_doc(ctx, req.params[0], json);
        return o ? send_data(req, o) : send_error(req, 500, "INTERNAL_ERROR", "Profile unreadable");
    });

    web.route(HTTP_GET, "/boards/{id}/pins", RouteAuth::kUser, [&ctx](ApiRequest &req) {
        BoardProfile p;
        if (!ctx.boards->load(req.params[0], p)) return send_error(req, 404, "NOT_FOUND", "Profile not found");
        std::string json;
        ctx.boards->get_json(req.params[0], json);
        BoardProfile parsed;
        bool ok = false;
        const ValidationReport report = ctx.boards->validate(json, parsed, ok);
        return send_data(req, pin_map_json(p, &report));
    });

    web.route(HTTP_POST, "/boards/{id}/duplicate", RouteAuth::kUser, [&ctx](ApiRequest &req) {
        std::string json;
        if (!ctx.boards->get_json(req.params[0], json)) return send_error(req, 404, "NOT_FOUND", "Profile not found");
        cJSON *doc = cJSON_Parse(json.c_str());
        if (doc == nullptr) return send_error(req, 500, "INTERNAL_ERROR", "Profile unreadable");
        std::string new_id = api::str_or(req.json(), "id", req.params[0] + "_custom");
        std::string name = api::str_or(req.json(), "name", "");
        for (int n = 2; ctx.boards->get_json(new_id, json) && n < 100; ++n) {
            new_id = req.params[0] + "_custom" + std::to_string(n);
        }
        cJSON_DeleteItemFromObjectCaseSensitive(doc, "id");
        cJSON_AddStringToObject(doc, "id", new_id.c_str());
        const cJSON *old_name = cJSON_GetObjectItemCaseSensitive(doc, "name");
        if (name.empty()) name = std::string(cJSON_IsString(old_name) ? old_name->valuestring : "Board") + " (custom)";
        cJSON_DeleteItemFromObjectCaseSensitive(doc, "name");
        cJSON_AddStringToObject(doc, "name", name.substr(0, 80).c_str());
        const std::string copy = api::to_string(doc);
        cJSON_Delete(doc);
        ValidationReport report;
        std::string id;
        const BoardOpResult r = ctx.boards->create(copy, report, id);
        if (r == BoardOpResult::kInvalid) return send_validation_failure(req, report);
        if (r != BoardOpResult::kOk) return send_error(req, 409, "CONFLICT", "Could not create the copy");
        ctx.boards->get_json(id, json);
        return send_data(req, profile_doc(ctx, id, json), 201);
    });

    web.route(HTTP_PUT, "/boards/{id}", RouteAuth::kUser, [&ctx](ApiRequest &req) {
        ValidationReport report;
        const BoardOpResult r = ctx.boards->update(req.params[0], req.body, report);
        if (r == BoardOpResult::kReadOnly)
            return send_error(req, 403, "READ_ONLY", "Built-in profiles are read-only - duplicate it first");
        if (r == BoardOpResult::kNotFound) return send_error(req, 404, "NOT_FOUND", "Profile not found");
        if (r == BoardOpResult::kInvalid) return send_validation_failure(req, report);
        if (r != BoardOpResult::kOk) return send_error(req, 500, "STORAGE_ERROR", "Could not save the profile");
        std::string json;
        ctx.boards->get_json(req.params[0], json);
        cJSON *o = profile_doc(ctx, req.params[0], json);
        if (req.params[0] == ctx.boards->active_id()) {
            cJSON_AddBoolToObject(o, "reapplyRequired", true);
        }
        return send_data(req, o);
    }, 32 * 1024);

    web.route(HTTP_DELETE, "/boards/{id}", RouteAuth::kUser, [&ctx](ApiRequest &req) {
        const BoardOpResult r = ctx.boards->remove(req.params[0]);
        if (r == BoardOpResult::kReadOnly) return send_error(req, 403, "READ_ONLY", "Built-in profiles cannot be deleted");
        if (r == BoardOpResult::kConflict) return send_error(req, 409, "CONFLICT", "The active profile cannot be deleted");
        if (r == BoardOpResult::kNotFound) return send_error(req, 404, "NOT_FOUND", "Profile not found");
        if (r != BoardOpResult::kOk) return send_error(req, 500, "STORAGE_ERROR", "Could not delete");
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "id", req.params[0].c_str());
        return send_data(req, o);
    });

    web.route(HTTP_POST, "/boards/{id}/apply", RouteAuth::kUser,
              [&ctx](ApiRequest &req) { return do_apply(ctx, req, req.params[0]); });

    web.route(HTTP_GET, "/hardware/status", RouteAuth::kUser, [&ctx](ApiRequest &req) {
        ModelLock lock(model_mutex());
        cJSON *o = cJSON_CreateObject();
        const BoardProfile &p = ctx.hardware->profile();
        cJSON_AddStringToObject(o, "active_board", p.id.c_str());
        cJSON_AddStringToObject(o, "active_board_name", p.name.c_str());
        cJSON_AddStringToObject(o, "mcu", p.mcu.c_str());
        cJSON_AddItemToObject(o, "outputs", outputs_json(ctx));
        cJSON_AddItemToObject(o, "inputs", inputs_json(ctx));
        cJSON *health = cJSON_AddArrayToObject(o, "peripherals");
        bool pca_present = false;
        cJSON *pcf = cJSON_AddObjectToObject(o, "pcf8574_health");
        for (const auto &h : ctx.hardware->health()) {
            cJSON *e = cJSON_CreateObject();
            cJSON_AddStringToObject(e, "id", h.id.c_str());
            cJSON_AddStringToObject(e, "type", h.type.c_str());
            cJSON_AddBoolToObject(e, "ok", h.ok);
            cJSON_AddNumberToObject(e, "errors", h.errors);
            cJSON_AddStringToObject(e, "detail", h.detail.c_str());
            cJSON_AddItemToArray(health, e);
            if (h.type == "pca9685" && h.ok) pca_present = true;
            if (h.type == "pcf8574") cJSON_AddNumberToObject(pcf, h.id.c_str(), h.errors);
        }
        cJSON_AddBoolToObject(o, "pca9685_present", pca_present);
        cJSON_AddBoolToObject(o, "ethernet_link", ctx.network->status().eth_link);
        cJSON_AddNumberToObject(o, "free_heap", esp_get_free_heap_size());
        cJSON_AddNumberToObject(o, "min_free_heap", esp_get_minimum_free_heap_size());
        const LightingStats &ls = ctx.lighting->stats();
        cJSON *lst = cJSON_AddObjectToObject(o, "lighting");
        cJSON_AddNumberToObject(lst, "hardware_writes", ls.hardware_writes);
        cJSON_AddNumberToObject(lst, "hardware_errors", ls.hardware_errors);
        cJSON_AddNumberToObject(lst, "queue_dropped", ls.queue_dropped);
        cJSON_AddNumberToObject(lst, "max_tick_us", ls.max_tick_us);
        return send_data(req, o);
    });

    web.route(HTTP_GET, "/hardware/inputs", RouteAuth::kUser, [&ctx](ApiRequest &req) {
        ModelLock lock(model_mutex());
        return send_data(req, inputs_json(ctx));
    });

    web.route(HTTP_GET, "/hardware/outputs", RouteAuth::kUser, [&ctx](ApiRequest &req) {
        ModelLock lock(model_mutex());
        return send_data(req, outputs_json(ctx));
    });

    // Hardware read-back (verifies what the chip actually holds).
    web.route(HTTP_GET, "/hardware/outputs/{id}/readback", RouteAuth::kUser, [&ctx](ApiRequest &req) {
        ModelLock lock(model_mutex());
        uint16_t level = 0;
        const esp_err_t err = ctx.hardware->read_output(req.params[0], level);
        if (err == ESP_ERR_NOT_FOUND) return send_error(req, 404, "NOT_FOUND", "Output not found");
        if (err != ESP_OK) return send_error(req, 503, "HARDWARE_ERROR", esp_err_to_name(err));
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "id", req.params[0].c_str());
        cJSON_AddNumberToObject(o, "level16", level);
        cJSON_AddNumberToObject(o, "percent", (level * 1000.0 / 65535.0) / 10.0);
        return send_data(req, o);
    });

    web.route(HTTP_POST, "/hardware/outputs/{id}/test", RouteAuth::kUser, [&ctx](ApiRequest &req) {
        double level = -1;
        bool on = false;
        if (!api::get_number(req.json(), "level", level)) {
            if (!api::get_bool(req.json(), "on", on)) {
                return send_error(req, 400, "INVALID_REQUEST", "level (0..100) or on (bool) required");
            }
            level = on ? 100 : 0;
        }
        if (level < 0 || level > 100) return send_error(req, 400, "INVALID_REQUEST", "level must be 0..100");
        ModelLock lock(model_mutex());
        const uint16_t raw = static_cast<uint16_t>(level * 655.35 + 0.5);
        const esp_err_t err = ctx.hardware->set_output(req.params[0], raw);
        if (err == ESP_ERR_NOT_FOUND) return send_error(req, 404, "NOT_FOUND", "Output not found");
        if (err != ESP_OK) return send_error(req, 503, "HARDWARE_ERROR", esp_err_to_name(err));
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "id", req.params[0].c_str());
        cJSON_AddNumberToObject(o, "level", level);
        return send_data(req, o);
    });

    web.route(HTTP_GET, "/hardware/i2c/scan", RouteAuth::kUser, [&ctx](ApiRequest &req) {
        const std::string bus_id = req.query_param("bus");
        II2CBus *bus = nullptr;
        BoardProfile profile;
        {
            ModelLock lock(model_mutex());
            bus = ctx.hardware->i2c_bus(bus_id);
            profile = ctx.hardware->profile();
        }
        if (bus == nullptr) return send_error(req, 404, "NOT_FOUND", "No working I2C bus in the active profile");
        // The bus serialises transactions itself; scanning without the model
        // lock keeps the lighting tick running during the ~120 probes.
        uint8_t found[112];
        std::size_t count = 0;
        const esp_err_t err = bus->scan(found, sizeof(found), count);
        if (err != ESP_OK) return send_error(req, 503, "HARDWARE_ERROR", esp_err_to_name(err));
        const BoardResource *res = nullptr;
        for (const auto &r : profile.resources) {
            if (r.type == "i2c" && (bus_id.empty() || r.id == bus_id)) { res = &r; break; }
        }
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "busId", res ? res->id.c_str() : "");
        cJSON_AddNumberToObject(o, "bus", res && res->port >= 0 ? res->port : 0);
        cJSON_AddNumberToObject(o, "sda_gpio", res ? res->pin("sda") : -1);
        cJSON_AddNumberToObject(o, "scl_gpio", res ? res->pin("scl") : -1);
        cJSON *devs = cJSON_AddArrayToObject(o, "devices");
        for (std::size_t i = 0; i < count; ++i) {
            const int addr = found[i];
            char hex[8];
            std::snprintf(hex, sizeof(hex), "0x%02X", addr);
            std::string label;
            for (const auto &p : profile.peripherals) {
                if (p.address == addr && (res == nullptr || p.bus == res->id)) label = p.id + " (" + p.type + ")";
            }
            if (label.empty() && addr == 0x70) {
                for (const auto &p : profile.peripherals) {
                    if (p.type == "pca9685") label = "PCA9685 all-call address";
                }
            }
            cJSON *d = cJSON_CreateObject();
            cJSON_AddNumberToObject(d, "address", addr);
            cJSON_AddStringToObject(d, "address_hex", hex);
            cJSON_AddBoolToObject(d, "known", !label.empty());
            if (!label.empty()) cJSON_AddStringToObject(d, "label", label.c_str());
            cJSON_AddBoolToObject(d, "unexpected", label.empty());
            cJSON_AddItemToArray(devs, d);
        }
        // Declared devices that did not answer (missing hardware / wrong address).
        cJSON *missing = cJSON_AddArrayToObject(o, "missing");
        for (const auto &p : profile.peripherals) {
            if (p.address < 0 || (res != nullptr && p.bus != res->id)) continue;
            bool seen = false;
            for (std::size_t i = 0; i < count; ++i) seen |= found[i] == p.address;
            if (!seen) cJSON_AddItemToArray(missing, cJSON_CreateString(p.id.c_str()));
        }
        return send_data(req, o);
    });
}

} // namespace lumen
