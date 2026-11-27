// Lumen — Lumen Cloud link routes (Lumen Pro P5; DECISIONS D-17).
//
//   GET  /cloud           pairing state, ids, ticket counters        (session or admin ticket)
//   POST /cloud/pair      {claimToken, gatewayUrl} -> 202, paired by the housekeeping task  (session only)
//   POST /cloud/unpair    forget the gateway and the local key        (session only)
//   PUT  /cloud/epoch     {ticketEpoch} only upwards: revokes older tickets (session or admin ticket;
//                         the gateway forwards the cloud's revocations this way)

#include <string>

#include "core/cJSON.h"
#include "lumen/app/app_context.hpp"
#include "lumen/app/cloud_service.hpp"
#include "lumen/web/api_util.hpp"

namespace lumen
{

void register_cloud_routes(AppContext &ctx)
{
    WebServer &web = *ctx.web;

    web.route(HTTP_GET, "/cloud", RouteAuth::kUser, [&ctx](ApiRequest &req) {
        return send_data(req, ctx.cloud->status_json());
    });

    web.route(
        HTTP_POST, "/cloud/pair", RouteAuth::kSession,
        [&ctx](ApiRequest &req) {
            cJSON *body = req.json();
            const cJSON *claim = cJSON_GetObjectItemCaseSensitive(body, "claimToken");
            const cJSON *url = cJSON_GetObjectItemCaseSensitive(body, "gatewayUrl");
            if (!cJSON_IsObject(body) || !cJSON_IsString(claim) || !cJSON_IsString(url)) {
                return send_error(req, 400, "INVALID_REQUEST", "Expected {claimToken, gatewayUrl}");
            }
            std::string error;
            if (!ctx.cloud->request_pair(claim->valuestring, url->valuestring, error)) {
                const bool busy = error.find("in progress") != std::string::npos;
                return send_error(req, busy ? 409 : 400, busy ? "BUSY" : "INVALID_REQUEST", error);
            }
            return send_data(req, ctx.cloud->status_json(), 202);
        },
        1024);

    web.route(HTTP_POST, "/cloud/unpair", RouteAuth::kSession, [&ctx](ApiRequest &req) {
        if (ctx.cloud->unpair() != ESP_OK) return send_error(req, 500, "STORAGE_ERROR", "Could not remove the pairing");
        return send_data(req, ctx.cloud->status_json());
    });

    web.route(
        HTTP_PUT, "/cloud/epoch", RouteAuth::kUser,
        [&ctx](ApiRequest &req) {
            const cJSON *e = cJSON_GetObjectItemCaseSensitive(req.json(), "ticketEpoch");
            if (!cJSON_IsNumber(e) || e->valuedouble < 0 || e->valuedouble > 4294967295.0 ||
                e->valuedouble != static_cast<double>(static_cast<uint32_t>(e->valuedouble))) {
                return send_error(req, 400, "INVALID_REQUEST", "ticketEpoch must be an integer 0-4294967295");
            }
            std::string error;
            if (!ctx.cloud->raise_epoch(static_cast<uint32_t>(e->valuedouble), error)) {
                return send_error(req, 409, "CONFLICT", error);
            }
            return send_data(req, ctx.cloud->status_json());
        },
        256);
}

} // namespace lumen
