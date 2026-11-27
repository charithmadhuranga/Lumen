// Lumen web server implementation. See web_server.hpp
#include "lumen/web/web_server.hpp"
#include "lumen/web/recovery_page.hpp"

#include <cstdio>
#include <cstring>
#include <sys/stat.h>

#include "esp_log.h"
#include "lumen/web/websocket_manager.hpp"
#include "lwip/sockets.h"

namespace lumen
{

namespace
{
const char *TAG = "web";
constexpr const char *kApiPrefix = "/api/v1";
constexpr const char *kCookieName = "lumen_session";

std::vector<std::string> split_path(const std::string &path)
{
    std::vector<std::string> out;
    std::size_t start = 0;
    while (start < path.size()) {
        if (path[start] == '/') {
            ++start;
            continue;
        }
        std::size_t end = path.find('/', start);
        if (end == std::string::npos) end = path.size();
        out.push_back(path.substr(start, end - start));
        start = end;
    }
    return out;
}

std::string url_decode(const std::string &in)
{
    std::string out;
    out.reserve(in.size());
    for (std::size_t i = 0; i < in.size(); ++i) {
        if (in[i] == '%' && i + 2 < in.size()) {
            char hex[3] = {in[i + 1], in[i + 2], 0};
            out.push_back(static_cast<char>(std::strtol(hex, nullptr, 16)));
            i += 2;
        } else if (in[i] == '+') {
            out.push_back(' ');
        } else {
            out.push_back(in[i]);
        }
    }
    return out;
}

const char *status_line(int status)
{
    switch (status) {
    case 200: return "200 OK";
    case 201: return "201 Created";
    case 202: return "202 Accepted";
    case 204: return "204 No Content";
    case 400: return "400 Bad Request";
    case 401: return "401 Unauthorized";
    case 403: return "403 Forbidden";
    case 404: return "404 Not Found";
    case 405: return "405 Method Not Allowed";
    case 409: return "409 Conflict";
    case 413: return "413 Payload Too Large";
    case 422: return "422 Unprocessable Entity";
    case 429: return "429 Too Many Requests";
    case 503: return "503 Service Unavailable";
    case 507: return "507 Insufficient Storage";
    default: return "500 Internal Server Error";
    }
}

struct Mime
{
    const char *ext;
    const char *type;
};
constexpr Mime kMime[] = {
    {".html", "text/html; charset=utf-8"},     {".js", "application/javascript"},
    {".css", "text/css"},                      {".json", "application/json"},
    {".svg", "image/svg+xml"},                 {".png", "image/png"},
    {".ico", "image/x-icon"},                  {".woff2", "font/woff2"},
    {".webmanifest", "application/manifest+json"}, {".txt", "text/plain"},
};

const char *mime_for(const std::string &path)
{
    const std::size_t dot = path.rfind('.');
    if (dot != std::string::npos) {
        const std::string ext = path.substr(dot);
        for (const auto &m : kMime) {
            if (ext == m.ext) return m.type;
        }
    }
    return "application/octet-stream";
}

bool file_exists(const std::string &path)
{
    struct stat st{};
    return stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}
} // namespace

// ---------------------------------------------------------------------------
// ApiRequest
// ---------------------------------------------------------------------------

cJSON *ApiRequest::json()
{
    if (!parsed_) {
        parsed_ = true;
        if (!body.empty()) {
            json_ = cJSON_ParseWithLength(body.data(), body.size());
        }
    }
    return json_;
}

std::string ApiRequest::query_param(const char *key) const
{
    char buf[128];
    if (query.empty() || httpd_query_key_value(query.c_str(), key, buf, sizeof(buf)) != ESP_OK) {
        return std::string();
    }
    return url_decode(buf);
}

ApiRequest::~ApiRequest()
{
    if (json_ != nullptr) {
        cJSON_Delete(json_);
    }
}

// ---------------------------------------------------------------------------
// Response helpers
// ---------------------------------------------------------------------------

void add_security_headers(httpd_req_t *req)
{
    httpd_resp_set_hdr(req, "X-Content-Type-Options", "nosniff");
    httpd_resp_set_hdr(req, "X-Frame-Options", "DENY");
    httpd_resp_set_hdr(req, "Referrer-Policy", "same-origin");
    httpd_resp_set_hdr(req, "Content-Security-Policy",
                       "default-src 'self'; img-src 'self' data: blob:; "
                       "style-src 'self' 'unsafe-inline'; script-src 'self'; "
                       "connect-src 'self' ws: wss:; object-src 'none'; "
                       "base-uri 'self'; frame-ancestors 'none'");
}

esp_err_t send_raw_json(httpd_req_t *req, int status, const std::string &body)
{
    httpd_resp_set_status(req, status_line(status));
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    add_security_headers(req);
    return httpd_resp_send(req, body.data(), static_cast<ssize_t>(body.size()));
}

esp_err_t send_data(ApiRequest &req, cJSON *data, int status)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "success", true);
    if (data != nullptr) {
        cJSON_AddItemToObject(root, "data", data);
    } else {
        cJSON_AddNullToObject(root, "data");
    }
    cJSON_AddNullToObject(root, "error");
    char *text = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (text == nullptr) {
        return send_error(req, 500, "INTERNAL_ERROR", "out of memory");
    }
    const esp_err_t err = send_raw_json(req.raw, status, text);
    cJSON_free(text);
    return err;
}

esp_err_t send_ok(ApiRequest &req)
{
    return send_data(req, nullptr, 200);
}

esp_err_t send_error(ApiRequest &req, int status, const char *code, const std::string &message)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "success", false);
    cJSON_AddNullToObject(root, "data");
    cJSON *err = cJSON_AddObjectToObject(root, "error");
    cJSON_AddStringToObject(err, "code", code);
    cJSON_AddStringToObject(err, "message", message.c_str());
    char *text = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    esp_err_t rc = ESP_FAIL;
    if (text != nullptr) {
        rc = send_raw_json(req.raw, status, text);
        cJSON_free(text);
    }
    return rc;
}

// ---------------------------------------------------------------------------
// Server
// ---------------------------------------------------------------------------

WebServer::WebServer(AuthManager &auth, const WebServerConfig &config)
    : auth_(auth), config_(config)
{
}

WebServer::~WebServer()
{
    stop();
}

void WebServer::route(int method, const char *pattern, RouteAuth auth, ApiHandler handler,
                      std::size_t max_body)
{
    routes_.push_back({method, split_path(pattern), auth, std::move(handler),
                       max_body != 0 ? max_body : config_.max_body});
}

void WebServer::route_stream(int method, const char *pattern, RouteAuth auth, ApiHandler handler,
                             std::size_t max_body)
{
    routes_.push_back({method, split_path(pattern), auth, std::move(handler), max_body, true});
}

esp_err_t WebServer::start()
{
    if (server_ != nullptr) {
        return ESP_OK;
    }
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.server_port = config_.port;
    cfg.max_open_sockets = config_.max_open_sockets;
    cfg.max_uri_handlers = 12;
    cfg.max_resp_headers = 16;
    cfg.stack_size = 10240;
    cfg.lru_purge_enable = true; // reclaim idle keep-alive sockets under load
    cfg.recv_wait_timeout = 10;
    cfg.send_wait_timeout = 10;
    cfg.uri_match_fn = httpd_uri_match_wildcard;
    cfg.task_priority = 5;

    if (pack_.load()) {
        ESP_LOGI(TAG, "web UI pack: %u assets, %u bytes", static_cast<unsigned>(pack_.count()),
                 static_cast<unsigned>(pack_.total_bytes()));
    } else {
        ESP_LOGW(TAG, "web UI pack missing or invalid - serving loose files only");
    }
    esp_err_t err = httpd_start(&server_, &cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start failed: %s", esp_err_to_name(err));
        server_ = nullptr;
        return err;
    }

    // Order matters: httpd matches handlers in registration order.
    httpd_uri_t ws{};
    ws.uri = "/api/v1/ws";
    ws.method = HTTP_GET;
    ws.handler = ws_trampoline;
    ws.user_ctx = this;
    ws.is_websocket = true;
    ws.handle_ws_control_frames = false;
    // ESP-IDF 6 performs the handshake itself and never calls the handler
    // for it: authenticate BEFORE the upgrade, register the client after.
    ws.ws_pre_handshake_cb = ws_pre_handshake;
    ws.ws_post_handshake_cb = ws_post_handshake;
    httpd_register_uri_handler(server_, &ws);

    const httpd_method_t methods[] = {HTTP_GET, HTTP_POST, HTTP_PUT, HTTP_DELETE};
    for (httpd_method_t m : methods) {
        httpd_uri_t api{};
        api.uri = "/api/v1/*";
        api.method = m;
        api.handler = api_trampoline;
        api.user_ctx = this;
        httpd_register_uri_handler(server_, &api);
    }
    httpd_uri_t opt{};
    opt.uri = "/*";
    opt.method = HTTP_OPTIONS;
    opt.handler = options_trampoline;
    opt.user_ctx = this;
    httpd_register_uri_handler(server_, &opt);

    httpd_uri_t st{};
    st.uri = "/*";
    st.method = HTTP_GET;
    st.handler = static_trampoline;
    st.user_ctx = this;
    httpd_register_uri_handler(server_, &st);

    ESP_LOGI(TAG, "HTTP server on port %u (%u API routes)", static_cast<unsigned>(config_.port),
             static_cast<unsigned>(routes_.size()));
    return ESP_OK;
}

esp_err_t WebServer::stop()
{
    if (server_ == nullptr) {
        return ESP_OK;
    }
    const esp_err_t err = httpd_stop(server_);
    server_ = nullptr;
    return err;
}

esp_err_t WebServer::api_trampoline(httpd_req_t *req)
{
    return static_cast<WebServer *>(req->user_ctx)->handle_api(req);
}

esp_err_t WebServer::static_trampoline(httpd_req_t *req)
{
    return static_cast<WebServer *>(req->user_ctx)->handle_static(req);
}

esp_err_t WebServer::options_trampoline(httpd_req_t *req)
{
    // Same-origin app: no CORS grants. Answer preflights with 204 only.
    add_security_headers(req);
    httpd_resp_set_status(req, "204 No Content");
    return httpd_resp_send(req, nullptr, 0);
}

esp_err_t WebServer::ws_pre_handshake(httpd_req_t *req)
{
    // Runs before the 101 upgrade: require a valid, password-changed session,
    // or (Lumen Pro) any valid local ticket in the header or ?ticket=.
    auto *self = static_cast<WebServer *>(req->user_ctx);
    const std::string ticket = ticket_from(req, true);
    if (!ticket.empty()) {
        if (self->ws_ != nullptr && self->tickets_ && self->tickets_(ticket).ok) return ESP_OK;
        ESP_LOGW(TAG, "WS handshake rejected (invalid ticket)");
        httpd_resp_set_status(req, "401 Unauthorized");
        httpd_resp_send(req, nullptr, 0);
        return ESP_FAIL;
    }
    const std::string token = cookie_token(req);
    AuthResult res = self->auth_.validate_session(token);
    if (self->ws_ == nullptr || res.code != AuthResult::kOk || res.session->force_pw_change) {
        ESP_LOGW(TAG, "WS handshake rejected (no valid session)");
        httpd_resp_set_status(req, "401 Unauthorized");
        httpd_resp_send(req, nullptr, 0);
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t WebServer::ws_post_handshake(httpd_req_t *req)
{
    auto *self = static_cast<WebServer *>(req->user_ctx);
    // The connection remembers its credential (session token or ticket); the
    // periodic re-check closes it once that stops being valid.
    const std::string ticket = ticket_from(req, true);
    return self->ws_ != nullptr ? self->ws_->on_open(req, ticket.empty() ? cookie_token(req) : ticket) : ESP_FAIL;
}

esp_err_t WebServer::ws_trampoline(httpd_req_t *req)
{
    // Data frames only (IDF 6 never routes the handshake here).
    auto *self = static_cast<WebServer *>(req->user_ctx);
    return self->ws_ != nullptr ? self->ws_->on_frame(req) : ESP_FAIL;
}

std::string WebServer::ticket_from(httpd_req_t *req, bool allow_query)
{
    constexpr std::size_t kMax = 600; // tickets are at most 512 bytes (PROTOCOL §6)
    char buf[kMax + 16];
    const std::size_t len = httpd_req_get_hdr_value_len(req, "Authorization");
    if (len > 0 && len < sizeof(buf) && httpd_req_get_hdr_value_str(req, "Authorization", buf, sizeof(buf)) == ESP_OK) {
        static constexpr char kScheme[] = "Lumen-Ticket ";
        if (std::strncmp(buf, kScheme, sizeof(kScheme) - 1) == 0) return std::string(buf + sizeof(kScheme) - 1);
    } else if (len >= sizeof(buf)) {
        return std::string(kMax + 1, 'x'); // too long: fails verification as "format"
    }
    if (allow_query) {
        const std::size_t qlen = httpd_req_get_url_query_len(req);
        if (qlen > 0 && qlen < sizeof(buf) && httpd_req_get_url_query_str(req, buf, sizeof(buf)) == ESP_OK) {
            char value[kMax + 1];
            if (httpd_query_key_value(buf, "ticket", value, sizeof(value)) == ESP_OK) return std::string(value);
        }
    }
    return std::string();
}

std::string WebServer::cookie_token(httpd_req_t *req)
{
    char value[80];
    std::size_t len = sizeof(value);
    if (httpd_req_get_cookie_val(req, kCookieName, value, &len) != ESP_OK) {
        return std::string();
    }
    return std::string(value);
}

void WebServer::set_session_cookie(httpd_req_t *req, const std::string &token, int64_t max_age_s)
{
    // The header value must outlive httpd_resp_send(): keep it per-task.
    static thread_local char cookie[160];
    std::snprintf(cookie, sizeof(cookie), "%s=%s; Path=/; HttpOnly; SameSite=Strict; Max-Age=%lld",
                  kCookieName, token.c_str(), static_cast<long long>(max_age_s));
    httpd_resp_set_hdr(req, "Set-Cookie", cookie);
}

void WebServer::clear_session_cookie(httpd_req_t *req)
{
    static const char *cookie = "lumen_session=; Path=/; HttpOnly; SameSite=Strict; Max-Age=0";
    httpd_resp_set_hdr(req, "Set-Cookie", cookie);
}

std::string WebServer::client_ip(httpd_req_t *req)
{
    const int fd = httpd_req_to_sockfd(req);
    struct sockaddr_in6 addr{};
    socklen_t len = sizeof(addr);
    if (getpeername(fd, reinterpret_cast<struct sockaddr *>(&addr), &len) != 0) {
        return "unknown";
    }
    char buf[48] = "unknown";
    if (addr.sin6_family == AF_INET) {
        inet_ntop(AF_INET, &reinterpret_cast<struct sockaddr_in *>(&addr)->sin_addr, buf, sizeof(buf));
    } else {
        // IPv4-mapped IPv6 (lwIP dual stack): last 4 bytes are the IPv4 address.
        const uint8_t *b = reinterpret_cast<const uint8_t *>(&addr.sin6_addr);
        std::snprintf(buf, sizeof(buf), "%u.%u.%u.%u", b[12], b[13], b[14], b[15]);
    }
    return buf;
}

esp_err_t WebServer::handle_api(httpd_req_t *req)
{
    ++requests_;
    ApiRequest ar;
    ar.raw = req;
    ar.method = req->method;
    std::string uri = req->uri;
    const std::size_t q = uri.find('?');
    if (q != std::string::npos) {
        ar.query = uri.substr(q + 1);
        uri.resize(q);
    }
    const std::size_t prefix_len = std::strlen(kApiPrefix);
    ar.path = uri.size() > prefix_len ? uri.substr(prefix_len) : "/";
    const std::vector<std::string> segs = split_path(ar.path);

    const Route *match = nullptr;
    bool path_known = false;
    for (const auto &r : routes_) {
        if (r.segments.size() != segs.size()) continue;
        bool ok = true;
        std::vector<std::string> params;
        for (std::size_t i = 0; i < segs.size() && ok; ++i) {
            const std::string &p = r.segments[i];
            if (p.size() > 2 && p.front() == '{' && p.back() == '}') {
                params.push_back(url_decode(segs[i]));
            } else if (p != segs[i]) {
                ok = false;
            }
        }
        if (!ok) continue;
        path_known = true;
        if (r.method == req->method) {
            match = &r;
            ar.params = std::move(params);
            break;
        }
    }
    if (match == nullptr) {
        return path_known ? send_error(ar, 405, "METHOD_NOT_ALLOWED", "Method not allowed")
                          : send_error(ar, 404, "NOT_FOUND", "API endpoint not found");
    }

    // --- authentication / CSRF / forced password change -----------------
    ar.client_ip = client_ip(req);
    // Lumen Pro: a local ticket instead of a session (PROTOCOL.md §6). Tickets
    // are not ambient credentials (the client attaches them on purpose), so no
    // CSRF token applies. Some routes never accept one (RouteAuth::kSession).
    const std::string ticket = ticket_from(req, false);
    if (!ticket.empty()) {
        if (!tickets_) return send_error(ar, 401, "INVALID_TICKET", "This controller is not paired with a gateway");
        const TicketVerdict v = tickets_(ticket);
        if (!v.ok) return send_error(ar, 401, "INVALID_TICKET", std::string("Ticket rejected: ") + v.reason);
        if (match->auth == RouteAuth::kPasswordChange || match->auth == RouteAuth::kSession) {
            return send_error(ar, 403, "SESSION_REQUIRED", "Sign in on the controller itself for this");
        }
        if (match->auth == RouteAuth::kUser && !v.admin) {
            return send_error(ar, 403, "FORBIDDEN", "This needs admin access");
        }
        ar.via_ticket = true;
        ar.ticket_admin = v.admin;
    }
    const std::string token = ar.via_ticket ? std::string() : cookie_token(req);
    if (!token.empty()) {
        AuthResult res = auth_.validate_session(token);
        if (res.code == AuthResult::kOk) {
            ar.session_token = token;
            ar.session = *res.session;
        }
    }
    if (match->auth != RouteAuth::kPublic && !ar.via_ticket) {
        if (ar.session_token.empty()) {
            return send_error(ar, 401, "UNAUTHORIZED", "Authentication required");
        }
        if (match->auth != RouteAuth::kPasswordChange && ar.session.force_pw_change) {
            return send_error(ar, 403, "PASSWORD_CHANGE_REQUIRED",
                              "The default password must be changed first");
        }
        if (req->method != HTTP_GET) {
            char csrf[48];
            const esp_err_t herr =
                httpd_req_get_hdr_value_str(req, "X-CSRF-Token", csrf, sizeof(csrf));
            if (herr != ESP_OK || !auth_.check_csrf(token, csrf)) {
                return send_error(ar, 403, "CSRF_FAILED", "Missing or invalid CSRF token");
            }
        }
    }

    // --- bounded body ----------------------------------------------------
    if (req->content_len > match->max_body) {
        return send_error(ar, 413, "PAYLOAD_TOO_LARGE",
                          "Request body exceeds " + std::to_string(match->max_body) + " bytes");
    }
    if (match->stream) return match->handler(ar); // handler consumes the body
    if (req->content_len > 0) {
        ar.body.resize(req->content_len);
        std::size_t got = 0;
        while (got < req->content_len) {
            const int n = httpd_req_recv(req, &ar.body[got], req->content_len - got);
            if (n == HTTPD_SOCK_ERR_TIMEOUT) continue;
            if (n <= 0) {
                return ESP_FAIL; // socket closed; httpd cleans up
            }
            got += static_cast<std::size_t>(n);
        }
    }
    return match->handler(ar);
}

bool WebServer::serve_file(httpd_req_t *req, const std::string &path, bool is_index)
{
    // 1. Packed UI (production image): one file, assets served by offset.
    const std::string root = config_.static_root;
    WebPack::Entry entry;
    if (path.size() > root.size() + 1 && pack_.find(path.substr(root.size() + 1), entry)) {
        FILE *pf = std::fopen(pack_.file().c_str(), "rb");
        if (pf == nullptr || std::fseek(pf, static_cast<long>(entry.offset), SEEK_SET) != 0) {
            if (pf) std::fclose(pf);
            return false;
        }
        httpd_resp_set_type(req, mime_for(path));
        httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
        httpd_resp_set_hdr(req, "Cache-Control", is_index ? "no-cache"
                                                 : path.find("/assets/") != std::string::npos
                                                     ? "public, max-age=31536000, immutable"
                                                     : "public, max-age=3600");
        add_security_headers(req);
        char buf[2048];
        uint32_t left = entry.length;
        while (left > 0) {
            const std::size_t n = std::fread(buf, 1, left < sizeof(buf) ? left : sizeof(buf), pf);
            if (n == 0 || httpd_resp_send_chunk(req, buf, static_cast<ssize_t>(n)) != ESP_OK) break;
            left -= static_cast<uint32_t>(n);
        }
        std::fclose(pf);
        httpd_resp_send_chunk(req, nullptr, 0);
        return true;
    }
    // 2. Loose files (development / custom uploads).
    std::string file = path;
    bool gz = false;
    char enc[64] = {};
    const bool accepts_gzip =
        httpd_req_get_hdr_value_str(req, "Accept-Encoding", enc, sizeof(enc)) == ESP_OK &&
        std::strstr(enc, "gzip") != nullptr;
    // The UI image stores assets gzip-only; every browser accepts gzip, so
    // the .gz copy is served even when the header is missing.
    if (file_exists(path + ".gz") && (accepts_gzip || !file_exists(path))) {
        file = path + ".gz";
        gz = true;
    } else if (!file_exists(path)) {
        return false;
    }
    FILE *f = std::fopen(file.c_str(), "rb");
    if (f == nullptr) {
        return false;
    }
    httpd_resp_set_type(req, mime_for(path));
    if (gz) {
        httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
    }
    if (is_index) {
        httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
    } else if (path.find("/assets/") != std::string::npos) {
        httpd_resp_set_hdr(req, "Cache-Control", "public, max-age=31536000, immutable");
    } else {
        httpd_resp_set_hdr(req, "Cache-Control", "public, max-age=3600");
    }
    add_security_headers(req);
    char buf[2048];
    std::size_t n = 0;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) {
        if (httpd_resp_send_chunk(req, buf, static_cast<ssize_t>(n)) != ESP_OK) {
            std::fclose(f);
            httpd_resp_sendstr_chunk(req, nullptr);
            return true;
        }
    }
    std::fclose(f);
    httpd_resp_send_chunk(req, nullptr, 0);
    return true;
}

esp_err_t WebServer::handle_static(httpd_req_t *req)
{
    ++requests_;
    std::string uri = req->uri;
    const std::size_t q = uri.find_first_of("?#");
    if (q != std::string::npos) uri.resize(q);
    if (uri.find("..") != std::string::npos) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad path");
        return ESP_OK;
    }
    if (uri.rfind("/api/", 0) == 0) {
        ApiRequest ar;
        ar.raw = req;
        return send_error(ar, 404, "NOT_FOUND", "API endpoint not found");
    }
    // Built-in recovery page: always available, independent of LittleFS.
    if (uri == "/recovery" || uri == "/recovery.js") {
        const bool js = uri == "/recovery.js";
        httpd_resp_set_type(req, js ? "text/javascript; charset=utf-8" : "text/html; charset=utf-8");
        httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
        add_security_headers(req);
        httpd_resp_sendstr(req, js ? kRecoveryJs : kRecoveryHtml);
        return ESP_OK;
    }
    const std::string root = config_.static_root;
    if (uri == "/" || uri.empty()) {
        uri = "/index.html";
    }
    if (serve_file(req, root + uri, uri == "/index.html")) {
        return ESP_OK;
    }
    // SPA fallback: unknown non-asset paths get index.html (DESIGN §14.2).
    const bool looks_like_asset = uri.rfind('.') != std::string::npos &&
                                  uri.rfind('.') > uri.rfind('/');
    if (!looks_like_asset && serve_file(req, root + "/index.html", true)) {
        return ESP_OK;
    }
    if (uri == "/index.html" || !looks_like_asset) {
        // No UI installed (first flash or interrupted UI update): the
        // recovery page lets the user upload one.
        httpd_resp_set_type(req, "text/html; charset=utf-8");
        httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
        add_security_headers(req);
        httpd_resp_sendstr(req, kRecoveryHtml);
        return ESP_OK;
    }
    httpd_resp_send_404(req);
    return ESP_OK;
}

} // namespace lumen
