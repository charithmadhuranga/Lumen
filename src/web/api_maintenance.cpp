// Lumen API — firmware/UI updates and configuration backup (SPEC §17.2, §20).
//
//   GET  /ota/status            running/other slot, update progress
//   POST /ota/upload            raw firmware.bin (application/octet-stream)
//   POST /system/ui/upload      raw web.pack
//   GET  /backup[?sections=a,b] configuration backup (JSON, no secrets)
//   POST /backup/restore[?mode=merge]  raw backup JSON; restarts on success
// Uploads are streamed (never buffered): the body is read by the handler.
#include <ctime>

#include "lumen/app/app_context.hpp"
#include "lumen/app/ota_service.hpp"
#include "lumen/core/model_lock.hpp"
#include "lumen/core/platform.hpp"
#include "lumen/services/backup.h"
#include "lumen/version.hpp"
#include "lumen/web/api_util.hpp"

namespace lumen
{

namespace
{

constexpr std::size_t kMaxFirmware = 0x1A0000; // OTA slot size (partitions.csv)
constexpr std::size_t kMaxUi = 600 * 1024;

// A rejected upload leaves unread bytes on the socket: close the connection
// instead of letting httpd drain megabytes.
esp_err_t reject(ApiRequest &req, int status, const char *code, const std::string &message)
{
    httpd_resp_set_hdr(req.raw, "Connection", "close");
    const esp_err_t r = send_error(req, status, code, message);
    httpd_sess_trigger_close(req.raw->handle, httpd_req_to_sockfd(req.raw));
    return r;
}

cJSON *status_json(const OtaStatus &s)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "state", s.state.c_str());
    cJSON_AddNumberToObject(o, "received", s.received);
    cJSON_AddNumberToObject(o, "total", s.total);
    cJSON_AddStringToObject(o, "error", s.error.c_str());
    cJSON_AddStringToObject(o, "newVersion", s.new_version.c_str());
    cJSON *run = cJSON_AddObjectToObject(o, "running");
    cJSON_AddStringToObject(run, "partition", s.running_partition.c_str());
    cJSON_AddStringToObject(run, "version", s.running_version.c_str());
    cJSON_AddStringToObject(run, "state", s.running_state.c_str());
    cJSON *oth = cJSON_AddObjectToObject(o, "other");
    cJSON_AddStringToObject(oth, "partition", s.other_partition.c_str());
    cJSON_AddStringToObject(oth, "version", s.other_version.c_str());
    cJSON_AddStringToObject(oth, "state", s.other_state.c_str());
    cJSON_AddNumberToObject(o, "slotSize", s.slot_size);
    cJSON_AddBoolToObject(o, "rollbackPossible", s.rollback_possible);
    return o;
}

std::vector<std::string> split_csv(const std::string &s)
{
    std::vector<std::string> out;
    std::size_t start = 0;
    while (start <= s.size()) {
        const std::size_t comma = s.find(',', start);
        const std::string part = s.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
        if (!part.empty() && part.size() <= 32) out.push_back(part);
        if (comma == std::string::npos) break;
        start = comma + 1;
    }
    return out;
}

} // namespace

void register_maintenance_routes(AppContext &ctx)
{
    WebServer &web = *ctx.web;

    web.route(HTTP_GET, "/ota/status", RouteAuth::kUser, [&ctx](ApiRequest &req) {
        return send_data(req, status_json(ctx.ota->status()));
    });

    web.route_stream(HTTP_POST, "/ota/upload", RouteAuth::kUser, [&ctx](ApiRequest &req) {
        if (ctx.ota->busy()) return reject(req, 409, "BUSY", "Another update is in progress");
        std::string error;
        if (!ctx.ota->upload_firmware(req.raw, error)) return reject(req, 400, "INVALID_FIRMWARE", error);
        cJSON *o = status_json(ctx.ota->status());
        cJSON_AddBoolToObject(o, "restarting", true);
        const esp_err_t r = send_data(req, o);
        ctx.schedule_restart(1500);
        return r;
    }, kMaxFirmware);

    web.route_stream(HTTP_POST, "/system/ui/upload", RouteAuth::kUser, [&ctx](ApiRequest &req) {
        if (ctx.ota->busy()) return reject(req, 409, "BUSY", "Another update is in progress");
        std::string error;
        const bool ok = ctx.ota->upload_ui(req.raw, ctx.web->pack().file(), error);
        ctx.web->pack().load(); // new pack, or none (the /recovery page then serves "/")
        if (!ok) return reject(req, 400, "INVALID_UI_PACK", error);
        cJSON *o = cJSON_CreateObject();
        cJSON_AddNumberToObject(o, "assets", static_cast<double>(ctx.web->pack().count()));
        cJSON_AddNumberToObject(o, "bytes", static_cast<double>(ctx.web->pack().total_bytes()));
        return send_data(req, o);
    }, kMaxUi);

    web.route(HTTP_GET, "/backup", RouteAuth::kUser, [&ctx](ApiRequest &req) {
        const std::vector<std::string> sections = split_csv(req.query_param("sections"));
        const NetworkStatus net = ctx.network->status();
        const std::time_t now = std::time(nullptr);
        const long long created = now > 1700000000 ? static_cast<long long>(now) : 0;
        std::string board;
        {
            ModelLock lock(model_mutex());
            board = ctx.hardware->profile().id;
        }
        // Streamed as the envelope's data so the UI client stays uniform.
        httpd_resp_set_type(req.raw, "application/json");
        httpd_resp_set_hdr(req.raw, "Cache-Control", "no-store");
        const std::string disposition = "attachment; filename=\"lumen-backup-" + net.hostname + ".json\"";
        httpd_resp_set_hdr(req.raw, "Content-Disposition", disposition.c_str());
        add_security_headers(req.raw);
        auto sink = [&req](const char *d, std::size_t n) {
            return httpd_resp_send_chunk(req.raw, d, static_cast<ssize_t>(n)) == ESP_OK;
        };
        bool ok = sink("{\"success\":true,\"data\":", 23);
        {
            // Consistent snapshot: no config writes while the documents are read.
            ModelLock lock(model_mutex());
            ok = ok && backup::write(kConfigRoot, kVersion, board, created, sections, sink);
        }
        ok = ok && sink(",\"error\":null}", 14);
        httpd_resp_send_chunk(req.raw, nullptr, 0);
        return ok ? ESP_OK : ESP_FAIL;
    });

    web.route_stream(HTTP_POST, "/backup/restore", RouteAuth::kUser, [&ctx](ApiRequest &req) {
        const bool merge = req.query_param("mode") == "merge";
        if (req.raw->content_len == 0) return reject(req, 400, "INVALID_REQUEST", "Upload a backup file");
        std::size_t left = req.raw->content_len;
        int timeouts = 0;
        auto source = [&](char *buf, std::size_t len) -> int {
            if (left == 0) return 0;
            for (;;) {
                const int n = httpd_req_recv(req.raw, buf, std::min(len, left));
                if (n == HTTPD_SOCK_ERR_TIMEOUT && ++timeouts <= 3) continue;
                if (n <= 0) return -1;
                left -= static_cast<std::size_t>(n);
                return n;
            }
        };
        std::string error;
        std::vector<std::string> restored;
        bool ok = false;
        {
            ModelLock lock(model_mutex());
            ok = backup::restore_stream(kConfigRoot, source, !merge, error, &restored);
            // The running model is now stale: freeze persistence until restart
            // so nothing overwrites the restored documents.
            if (ok) ctx.freeze_config();
        }
        if (!ok) return reject(req, 400, "INVALID_BACKUP", error);
        cJSON *o = cJSON_CreateObject();
        cJSON_AddItemToObject(o, "restored", api::string_array(restored));
        cJSON_AddBoolToObject(o, "restarting", true);
        const esp_err_t r = send_data(req, o);
        ctx.schedule_restart(1500);
        return r;
    }, backup::kMaxBytes);
}

} // namespace lumen
