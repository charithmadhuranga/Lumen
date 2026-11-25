// Lumen API — automations (SPEC §11, §17.2).
//
//   GET    /automations                 list with summary + runtime state
//   GET    /automations/settings        location, clock, today's sun times
//   PUT    /automations/settings        {latitude, longitude}
//   GET    /automations/history         recent executions (all)
//   GET    /automations/variables       PUT /automations/variables/{name} {value}
//   POST   /automations                 create (validated, references checked)
//   GET    /automations/{id}            stored document (+ graph) + runtime
//   PUT    /automations/{id}            replace
//   DELETE /automations/{id}
//   POST   /automations/{id}/enable | /disable | /test | /cancel
//   GET    /automations/{id}/history
#include <ctime>

#include "esp_timer.h"
#include "lumen/app/app_context.hpp"
#include "lumen/automation/automation_store.h"
#include "lumen/core/model_lock.hpp"
#include "lumen/web/api_util.hpp"

namespace lumen
{

namespace
{

uint32_t now_ms()
{
    return static_cast<uint32_t>(esp_timer_get_time() / 1000);
}

cJSON *runtime_json(const AutomationRuntime *rt)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddBoolToObject(o, "running", rt != nullptr && rt->running);
    cJSON_AddNumberToObject(o, "runs", rt ? rt->runs : 0);
    cJSON_AddNumberToObject(o, "errors", rt ? rt->errors : 0);
    cJSON_AddStringToObject(o, "lastResult", rt ? rt->last_result.c_str() : "");
    cJSON_AddNumberToObject(o, "lastRunUnix", rt ? static_cast<double>(rt->last_run_unix) : 0);
    return o;
}

cJSON *history_json(const std::vector<HistoryEntry> &hist)
{
    cJSON *arr = cJSON_CreateArray();
    for (const auto &h : hist) {
        cJSON *e = cJSON_CreateObject();
        cJSON_AddStringToObject(e, "automationId", h.automation_id.c_str());
        cJSON_AddStringToObject(e, "trigger", h.trigger.c_str());
        cJSON_AddNumberToObject(e, "unixTime", static_cast<double>(h.unix_time));
        cJSON_AddNumberToObject(e, "uptimeMs", h.uptime_ms);
        cJSON_AddNumberToObject(e, "durationMs", h.duration_ms);
        cJSON_AddStringToObject(e, "result", h.result.c_str());
        cJSON_AddStringToObject(e, "message", h.message.c_str());
        cJSON_AddItemToArray(arr, e);
    }
    return arr;
}

cJSON *summary_json(AppContext &ctx, const Automation &a)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "id", a.id.c_str());
    cJSON_AddStringToObject(o, "name", a.name.c_str());
    cJSON_AddBoolToObject(o, "enabled", a.enabled);
    cJSON_AddStringToObject(o, "mode", a.mode.c_str());
    cJSON_AddStringToObject(o, "triggerType", trigger_type_name(a.trigger.type));
    cJSON_AddStringToObject(o, "summary", automation_summary(a).c_str());
    cJSON_AddItemToObject(o, "runtime", runtime_json(ctx.automation->runtime(a.id)));
    return o;
}

// Parse + validate a body into an Automation; `graph` points into body.
// Returns false after sending the error response (caller must stop).
bool parse_body(AppContext &ctx, ApiRequest &req, Automation &a, const cJSON *&graph,
                const std::string &expected_id)
{
    cJSON *body = req.json();
    if (!cJSON_IsObject(body)) {
        send_error(req, 400, "INVALID_REQUEST", "JSON object expected");
        return false;
    }
    if (!expected_id.empty() && !cJSON_HasObjectItem(body, "id")) {
        cJSON_AddStringToObject(body, "id", expected_id.c_str());
    }
    std::string error;
    if (!automation_from_json(body, a, error)) {
        send_error(req, 400, "INVALID_CONFIGURATION", error);
        return false;
    }
    if (!expected_id.empty() && a.id != expected_id) {
        send_error(req, 400, "INVALID_CONFIGURATION", "id in body must match the URL");
        return false;
    }
    graph = cJSON_GetObjectItemCaseSensitive(body, "graph");
    if (graph != nullptr && !cJSON_IsObject(graph)) {
        send_error(req, 400, "INVALID_CONFIGURATION", "graph must be an object");
        return false;
    }
    if (!ctx.automation->validate(a, error)) {
        send_error(req, 400, "INVALID_CONFIGURATION", error);
        return false;
    }
    return true;
}

} // namespace

void register_automation_routes(AppContext &ctx)
{
    WebServer &web = *ctx.web;

    web.route(HTTP_GET, "/automations", RouteAuth::kUser, [&ctx](ApiRequest &req) {
        ModelLock lock(model_mutex());
        cJSON *arr = cJSON_CreateArray();
        for (const auto &a : ctx.automation->automations()) cJSON_AddItemToArray(arr, summary_json(ctx, a));
        return send_data(req, arr);
    });

    web.route(HTTP_GET, "/automations/settings", RouteAuth::kUser, [&ctx](ApiRequest &req) {
        const AutoClock c = ctx.automation_clock();
        double lat = 0, lon = 0;
        const bool has = ctx.get_location(lat, lon);
        cJSON *o = cJSON_CreateObject();
        cJSON_AddBoolToObject(o, "hasLocation", has);
        cJSON_AddNumberToObject(o, "latitude", lat);
        cJSON_AddNumberToObject(o, "longitude", lon);
        cJSON_AddBoolToObject(o, "timeSynced", c.valid);
        if (c.valid) {
            char buf[32];
            const std::time_t t = static_cast<std::time_t>(c.unix_time);
            std::tm local{};
            localtime_r(&t, &local);
            std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", &local);
            cJSON_AddStringToObject(o, "localTime", buf);
        }
        if (c.sunrise_min >= 0) cJSON_AddStringToObject(o, "sunrise", format_hhmm(static_cast<int16_t>(c.sunrise_min)).c_str());
        if (c.sunset_min >= 0) cJSON_AddStringToObject(o, "sunset", format_hhmm(static_cast<int16_t>(c.sunset_min)).c_str());
        return send_data(req, o);
    });

    web.route(HTTP_PUT, "/automations/settings", RouteAuth::kUser, [&ctx](ApiRequest &req) {
        double lat = 0, lon = 0;
        if (!api::get_number(req.json(), "latitude", lat) || !api::get_number(req.json(), "longitude", lon)) {
            return send_error(req, 400, "INVALID_REQUEST", "latitude and longitude are required");
        }
        ModelLock lock(model_mutex());
        if (!ctx.set_location(lat, lon)) {
            return send_error(req, 400, "INVALID_REQUEST", "latitude must be -90..90 and longitude -180..180");
        }
        return send_ok(req);
    });

    web.route(HTTP_GET, "/automations/history", RouteAuth::kUser, [&ctx](ApiRequest &req) {
        ModelLock lock(model_mutex());
        return send_data(req, history_json(ctx.automation->history()));
    });

    web.route(HTTP_GET, "/automations/variables", RouteAuth::kUser, [&ctx](ApiRequest &req) {
        ModelLock lock(model_mutex());
        cJSON *o = cJSON_CreateObject();
        for (const auto &kv : ctx.automation->variables()) cJSON_AddNumberToObject(o, kv.first.c_str(), kv.second);
        return send_data(req, o);
    });

    web.route(HTTP_PUT, "/automations/variables/{name}", RouteAuth::kUser, [&ctx](ApiRequest &req) {
        double v = 0;
        if (!api::get_number(req.json(), "value", v)) return send_error(req, 400, "INVALID_REQUEST", "value required");
        ModelLock lock(model_mutex());
        if (!ctx.automation->set_variable(req.params[0], v)) {
            return send_error(req, 400, "INVALID_REQUEST", "Variable name must be 1-24 characters (max 32 variables)");
        }
        return send_ok(req);
    });

    web.route(HTTP_POST, "/automations", RouteAuth::kUser, [&ctx](ApiRequest &req) {
        Automation a;
        const cJSON *graph = nullptr;
        ModelLock lock(model_mutex());
        if (!parse_body(ctx, req, a, graph, "")) return ESP_OK;
        if (ctx.automation->find(a.id) != nullptr) {
            return send_error(req, 409, "CONFLICT", "An automation with this id already exists");
        }
        if (ctx.automation->automations().size() >= kMaxAutomations) {
            return send_error(req, 409, "CAPACITY", "Automation limit reached (32)");
        }
        if (!automation_store::save(*ctx.storage, a, graph)) {
            return send_error(req, 500, "STORAGE_ERROR", "Could not save the automation");
        }
        ctx.automation->upsert(a);
        return send_data(req, summary_json(ctx, a), 201);
    }, 16 * 1024);

    web.route(HTTP_GET, "/automations/{id}", RouteAuth::kUser, [&ctx](ApiRequest &req) {
        ModelLock lock(model_mutex());
        const Automation *a = ctx.automation->find(req.params[0]);
        if (a == nullptr) return send_error(req, 404, "NOT_FOUND", "Automation not found");
        std::string text;
        cJSON *doc = automation_store::read_document(*ctx.storage, a->id, text) ? cJSON_Parse(text.c_str()) : nullptr;
        if (doc == nullptr) doc = automation_to_json(*a);
        cJSON_DeleteItemFromObjectCaseSensitive(doc, "enabled");
        cJSON_AddBoolToObject(doc, "enabled", a->enabled);
        cJSON_AddStringToObject(doc, "summary", automation_summary(*a).c_str());
        cJSON_AddItemToObject(doc, "runtime", runtime_json(ctx.automation->runtime(a->id)));
        return send_data(req, doc);
    });

    web.route(HTTP_PUT, "/automations/{id}", RouteAuth::kUser, [&ctx](ApiRequest &req) {
        Automation a;
        const cJSON *graph = nullptr;
        ModelLock lock(model_mutex());
        if (ctx.automation->find(req.params[0]) == nullptr) {
            return send_error(req, 404, "NOT_FOUND", "Automation not found");
        }
        if (!parse_body(ctx, req, a, graph, req.params[0])) return ESP_OK;
        if (!automation_store::save(*ctx.storage, a, graph)) {
            return send_error(req, 500, "STORAGE_ERROR", "Could not save the automation");
        }
        ctx.automation->cancel(a.id, now_ms()); // new definition replaces a running one
        ctx.automation->upsert(a);
        return send_data(req, summary_json(ctx, a));
    }, 16 * 1024);

    web.route(HTTP_DELETE, "/automations/{id}", RouteAuth::kUser, [&ctx](ApiRequest &req) {
        ModelLock lock(model_mutex());
        const std::string id = req.params[0];
        if (ctx.automation->find(id) == nullptr) return send_error(req, 404, "NOT_FOUND", "Automation not found");
        for (const auto &other : ctx.automation->automations()) {
            for (const auto &ac : other.actions) {
                if (ac.type == ActionType::kExecuteAutomation && ac.target == id && other.id != id) {
                    return send_error(req, 409, "IN_USE", "Used by automation '" + other.name + "'");
                }
            }
        }
        if (!automation_store::remove(*ctx.storage, id)) {
            return send_error(req, 500, "STORAGE_ERROR", "Could not delete");
        }
        ctx.automation->remove(id);
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "id", id.c_str());
        return send_data(req, o);
    });

    auto set_enabled = [&ctx](ApiRequest &req, bool enabled) {
        ModelLock lock(model_mutex());
        const Automation *cur = ctx.automation->find(req.params[0]);
        if (cur == nullptr) return send_error(req, 404, "NOT_FOUND", "Automation not found");
        Automation a = *cur;
        std::string error;
        if (enabled && !ctx.automation->validate(a, error)) {
            return send_error(req, 400, "INVALID_CONFIGURATION", "Cannot enable: " + error);
        }
        a.enabled = enabled;
        std::string text;
        cJSON *graph = nullptr;
        cJSON *doc = automation_store::read_document(*ctx.storage, a.id, text) ? cJSON_Parse(text.c_str()) : nullptr;
        if (doc != nullptr) graph = cJSON_GetObjectItemCaseSensitive(doc, "graph");
        const bool ok = automation_store::save(*ctx.storage, a, graph);
        cJSON_Delete(doc);
        if (!ok) return send_error(req, 500, "STORAGE_ERROR", "Could not save");
        ctx.automation->set_enabled(a.id, enabled);
        return send_data(req, summary_json(ctx, *ctx.automation->find(a.id)));
    };
    web.route(HTTP_POST, "/automations/{id}/enable", RouteAuth::kUser, [set_enabled](ApiRequest &req) { return set_enabled(req, true); });
    web.route(HTTP_POST, "/automations/{id}/disable", RouteAuth::kUser, [set_enabled](ApiRequest &req) { return set_enabled(req, false); });

    web.route(HTTP_POST, "/automations/{id}/test", RouteAuth::kUser, [&ctx](ApiRequest &req) {
        const AutoClock clock = ctx.automation_clock();
        ModelLock lock(model_mutex());
        std::string error;
        if (!ctx.automation->run_now(req.params[0], now_ms(), clock, error)) {
            return send_error(req, error == "automation not found" ? 404 : 409, "TEST_FAILED", error);
        }
        cJSON *o = cJSON_CreateObject();
        cJSON_AddBoolToObject(o, "started", true);
        cJSON_AddItemToObject(o, "runtime", runtime_json(ctx.automation->runtime(req.params[0])));
        return send_data(req, o, 202);
    });

    web.route(HTTP_POST, "/automations/{id}/cancel", RouteAuth::kUser, [&ctx](ApiRequest &req) {
        ModelLock lock(model_mutex());
        if (ctx.automation->find(req.params[0]) == nullptr) return send_error(req, 404, "NOT_FOUND", "Automation not found");
        ctx.automation->cancel(req.params[0], now_ms());
        return send_ok(req);
    });

    web.route(HTTP_GET, "/automations/{id}/history", RouteAuth::kUser, [&ctx](ApiRequest &req) {
        ModelLock lock(model_mutex());
        return send_data(req, history_json(ctx.automation->history(req.params[0])));
    });
}

} // namespace lumen
