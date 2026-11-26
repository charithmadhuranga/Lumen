// Lumen API — floor plans (SPEC §10, §17.2).
//
//   GET    /floorplans                 list (id, name, viewBox, hasSvg, counts)
//   POST   /floorplans                 {id?, name} (max 3)
//   GET    /floorplans/{id}            overlay document
//   PUT    /floorplans/{id}            {name}
//   DELETE /floorplans/{id}            overlay + SVG
//   POST   /floorplans/{id}/upload     raw SVG (≤ 128 KB), checked before it is stored
//   GET    /floorplans/{id}/svg        the stored SVG, sandboxed (the UI sanitises it again)
//   PUT    /floorplans/{id}/layout     {lights:[…], rooms:[…]} in viewBox coordinates
// Overlay JSON lives in config/floorplans/ (part of backups); the SVG lives in
// /lfs/floorplans/ and is never modified by Lumen.
#include <cstdio>
#include <memory>
#include <sys/stat.h>
#include <unistd.h>

#include "lumen/app/app_context.hpp"
#include "lumen/core/model_lock.hpp"
#include "lumen/core/platform.hpp"
#include "lumen/services/floorplan.h"
#include "lumen/services/svg_check.h"
#include "lumen/web/api_util.hpp"

namespace lumen
{

namespace
{

constexpr const char *kSvgDir = "/lfs/floorplans";
constexpr std::size_t kFsMargin = 24 * 1024;

std::string doc_path(const std::string &id) { return "floorplans/" + id + ".json"; }
std::string svg_path(const std::string &id) { return std::string(kSvgDir) + "/" + id + ".svg"; }

bool load(AppContext &ctx, const std::string &id, floorplan::Plan &plan)
{
    std::string text, err;
    if (!floorplan::valid_id(id) || ctx.storage->read(doc_path(id), text) != StorageError::kOk) return false;
    cJSON *o = cJSON_Parse(text.c_str());
    const bool ok = o != nullptr && floorplan::from_json(o, plan, err) && plan.id == id;
    cJSON_Delete(o);
    // The drawing is not part of backups: report what is actually stored.
    struct stat st{};
    plan.has_svg = ok && stat(svg_path(id).c_str(), &st) == 0 && st.st_size > 0;
    return ok;
}

bool save(AppContext &ctx, const floorplan::Plan &plan)
{
    cJSON *o = floorplan::to_json(plan);
    const std::string text = api::to_string(o);
    cJSON_Delete(o);
    return ctx.storage->write(doc_path(plan.id), text) == StorageError::kOk;
}

std::vector<std::string> plan_ids(AppContext &ctx)
{
    std::vector<std::string> ids;
    for (const auto &p : ctx.storage->list("floorplans")) {
        const std::size_t slash = p.rfind('/');
        const std::string name = slash == std::string::npos ? p : p.substr(slash + 1);
        if (name.size() > 5 && name.compare(name.size() - 5, 5, ".json") == 0) {
            const std::string id = name.substr(0, name.size() - 5);
            if (floorplan::valid_id(id)) ids.push_back(id);
        }
    }
    return ids;
}

cJSON *summary(const floorplan::Plan &p)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "id", p.id.c_str());
    cJSON_AddStringToObject(o, "name", p.name.c_str());
    cJSON *vb = cJSON_AddArrayToObject(o, "viewBox");
    for (double v : p.vb) cJSON_AddItemToArray(vb, cJSON_CreateNumber(v));
    cJSON_AddBoolToObject(o, "hasSvg", p.has_svg);
    cJSON_AddNumberToObject(o, "lights", static_cast<double>(p.lights.size()));
    cJSON_AddNumberToObject(o, "rooms", static_cast<double>(p.rooms.size()));
    return o;
}

esp_err_t reject(ApiRequest &req, int status, const char *code, const std::string &message)
{
    httpd_resp_set_hdr(req.raw, "Connection", "close");
    const esp_err_t r = send_error(req, status, code, message);
    httpd_sess_trigger_close(req.raw->handle, httpd_req_to_sockfd(req.raw));
    return r;
}

std::string slug(const std::string &name)
{
    std::string out;
    for (char c : name) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) out.push_back(c);
        else if (!out.empty() && out.back() != '-') out.push_back('-');
        if (out.size() >= 24) break;
    }
    while (!out.empty() && out.back() == '-') out.pop_back();
    return out.empty() ? "plan" : out;
}

} // namespace

void register_floorplan_routes(AppContext &ctx)
{
    WebServer &web = *ctx.web;
    mkdir(kSvgDir, 0755);

    web.route(HTTP_GET, "/floorplans", RouteAuth::kUser, [&ctx](ApiRequest &req) {
        ModelLock lock(model_mutex());
        cJSON *arr = cJSON_CreateArray();
        for (const auto &id : plan_ids(ctx)) {
            floorplan::Plan p;
            if (load(ctx, id, p)) cJSON_AddItemToArray(arr, summary(p));
        }
        return send_data(req, arr);
    });

    web.route(HTTP_POST, "/floorplans", RouteAuth::kUser, [&ctx](ApiRequest &req) {
        cJSON *b = req.json();
        floorplan::Plan p;
        if (!api::get_string(b, "name", p.name, 49) || p.name.empty()) {
            return send_error(req, 400, "INVALID_REQUEST", "name is required (1-48 characters)");
        }
        ModelLock lock(model_mutex());
        const auto ids = plan_ids(ctx);
        if (ids.size() >= floorplan::kMaxPlans) return send_error(req, 409, "CAPACITY", "At most 3 floor plans");
        api::get_string(b, "id", p.id, 33);
        if (p.id.empty()) {
            const std::string base = slug(p.name);
            p.id = base;
            for (int n = 2; std::find(ids.begin(), ids.end(), p.id) != ids.end(); ++n) p.id = base + "-" + std::to_string(n);
        }
        if (!floorplan::valid_id(p.id)) return send_error(req, 400, "INVALID_REQUEST", "id: 1-32 of a-z 0-9 _ -");
        if (std::find(ids.begin(), ids.end(), p.id) != ids.end()) return send_error(req, 409, "CONFLICT", "Floor plan exists");
        if (!save(ctx, p)) return send_error(req, 500, "STORAGE_ERROR", "Could not save");
        return send_data(req, floorplan::to_json(p), 201);
    });

    web.route(HTTP_GET, "/floorplans/{id}", RouteAuth::kUser, [&ctx](ApiRequest &req) {
        ModelLock lock(model_mutex());
        floorplan::Plan p;
        if (!load(ctx, req.params[0], p)) return send_error(req, 404, "NOT_FOUND", "Floor plan not found");
        return send_data(req, floorplan::to_json(p));
    });

    web.route(HTTP_PUT, "/floorplans/{id}", RouteAuth::kUser, [&ctx](ApiRequest &req) {
        ModelLock lock(model_mutex());
        floorplan::Plan p;
        if (!load(ctx, req.params[0], p)) return send_error(req, 404, "NOT_FOUND", "Floor plan not found");
        std::string name;
        if (!api::get_string(req.json(), "name", name, 49) || name.empty()) {
            return send_error(req, 400, "INVALID_REQUEST", "name is required (1-48 characters)");
        }
        p.name = name;
        if (!save(ctx, p)) return send_error(req, 500, "STORAGE_ERROR", "Could not save");
        return send_data(req, floorplan::to_json(p));
    });

    web.route(HTTP_DELETE, "/floorplans/{id}", RouteAuth::kUser, [&ctx](ApiRequest &req) {
        ModelLock lock(model_mutex());
        floorplan::Plan p;
        if (!load(ctx, req.params[0], p)) return send_error(req, 404, "NOT_FOUND", "Floor plan not found");
        if (ctx.storage->remove(doc_path(p.id)) != StorageError::kOk) return send_error(req, 500, "STORAGE_ERROR", "Could not delete");
        unlink(svg_path(p.id).c_str());
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "id", p.id.c_str());
        return send_data(req, o);
    });

    web.route(HTTP_PUT, "/floorplans/{id}/layout", RouteAuth::kUser, [&ctx](ApiRequest &req) {
        ModelLock lock(model_mutex());
        floorplan::Plan p;
        if (!load(ctx, req.params[0], p)) return send_error(req, 404, "NOT_FOUND", "Floor plan not found");
        std::vector<std::string> lights, rooms;
        const ChannelManager &cm = ctx.lighting->channel_manager();
        for (std::size_t i = 0; i < cm.channel_count(); ++i) lights.push_back(cm.at(i).id);
        for (std::size_t i = 0; i < ctx.rooms->room_count(); ++i) rooms.push_back(ctx.rooms->at(i)->id);
        std::string error;
        if (!floorplan::apply_layout(req.json(), lights, rooms, p, error)) {
            return send_error(req, 400, "INVALID_CONFIGURATION", error);
        }
        if (!save(ctx, p)) return send_error(req, 500, "STORAGE_ERROR", "Could not save the layout");
        return send_data(req, floorplan::to_json(p));
    }, 24 * 1024);

    web.route_stream(HTTP_POST, "/floorplans/{id}/upload", RouteAuth::kUser, [&ctx](ApiRequest &req) {
        const std::string id = req.params[0];
        floorplan::Plan p;
        {
            ModelLock lock(model_mutex());
            if (!load(ctx, id, p)) return reject(req, 404, "NOT_FOUND", "Floor plan not found");
        }
        const std::size_t total = req.raw->content_len;
        if (total == 0) return reject(req, 400, "INVALID_SVG", "Upload an SVG file");
        FsUsage fs;
        if (platform_fs_usage(fs) != ESP_OK || fs.total < fs.used + total + kFsMargin) {
            return reject(req, 507, "STORAGE_FULL", "Not enough storage for this floor plan");
        }
        const std::string tmp = svg_path(id) + ".new";
        FILE *f = std::fopen(tmp.c_str(), "wb");
        if (f == nullptr) return reject(req, 500, "STORAGE_ERROR", "Could not create the file");
        svg::Checker checker;
        std::unique_ptr<char[]> buf(new (std::nothrow) char[2048]);
        bool ok = buf != nullptr, io = true;
        std::size_t got = 0;
        int timeouts = 0;
        while (ok && got < total) {
            const int n = httpd_req_recv(req.raw, buf.get(), std::min<std::size_t>(2048, total - got));
            if (n == HTTPD_SOCK_ERR_TIMEOUT && ++timeouts <= 3) continue;
            if (n <= 0) {
                ok = io = false;
                break;
            }
            got += static_cast<std::size_t>(n);
            ok = checker.feed(buf.get(), static_cast<std::size_t>(n)) &&
                 std::fwrite(buf.get(), 1, static_cast<std::size_t>(n), f) == static_cast<std::size_t>(n);
        }
        ok = std::fclose(f) == 0 && ok && checker.finish();
        if (!ok) {
            unlink(tmp.c_str());
            if (!io) return reject(req, 400, "INVALID_SVG", "Upload interrupted");
            return reject(req, 400, "INVALID_SVG", checker.error().empty() ? "Could not store the file" : "Unsafe or invalid SVG: " + checker.error());
        }
        ModelLock lock(model_mutex());
        unlink(svg_path(id).c_str());
        if (rename(tmp.c_str(), svg_path(id).c_str()) != 0) {
            unlink(tmp.c_str());
            return send_error(req, 500, "STORAGE_ERROR", "Could not store the file");
        }
        const svg::ViewBox vb = checker.view_box();
        const bool changed = p.vb[2] != vb.w || p.vb[3] != vb.h || p.vb[0] != vb.x || p.vb[1] != vb.y;
        p.vb[0] = vb.x;
        p.vb[1] = vb.y;
        p.vb[2] = vb.w;
        p.vb[3] = vb.h;
        p.has_svg = true;
        // A drawing with a different coordinate system invalidates positions.
        if (changed && (!p.lights.empty() || !p.rooms.empty())) {
            p.lights.clear();
            p.rooms.clear();
        }
        if (!save(ctx, p)) return send_error(req, 500, "STORAGE_ERROR", "Could not save the floor plan");
        cJSON *o = floorplan::to_json(p);
        cJSON_AddNumberToObject(o, "elements", static_cast<double>(checker.elements()));
        cJSON_AddBoolToObject(o, "layoutReset", changed);
        return send_data(req, o);
    }, svg::kMaxBytes);

    web.route(HTTP_GET, "/floorplans/{id}/svg", RouteAuth::kUser, [&ctx](ApiRequest &req) {
        if (!floorplan::valid_id(req.params[0])) return send_error(req, 404, "NOT_FOUND", "Floor plan not found");
        FILE *f = std::fopen(svg_path(req.params[0]).c_str(), "rb");
        if (f == nullptr) return send_error(req, 404, "NOT_FOUND", "No drawing uploaded");
        httpd_resp_set_type(req.raw, "image/svg+xml");
        httpd_resp_set_hdr(req.raw, "Cache-Control", "no-store");
        httpd_resp_set_hdr(req.raw, "X-Content-Type-Options", "nosniff");
        httpd_resp_set_hdr(req.raw, "X-Frame-Options", "DENY");
        // Opened directly, the document cannot run anything or load resources.
        httpd_resp_set_hdr(req.raw, "Content-Security-Policy", "default-src 'none'; style-src 'unsafe-inline'; img-src data:; sandbox");
        char buf[1024];
        std::size_t n;
        esp_err_t r = ESP_OK;
        while (r == ESP_OK && (n = std::fread(buf, 1, sizeof(buf), f)) > 0) r = httpd_resp_send_chunk(req.raw, buf, static_cast<ssize_t>(n));
        std::fclose(f);
        httpd_resp_send_chunk(req.raw, nullptr, 0);
        return r;
    });
}

} // namespace lumen
