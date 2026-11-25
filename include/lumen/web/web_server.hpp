// Lumen web server — ESP-IDF httpd with a small router (SPEC §16–17,
// DESIGN §13–14).
//
// * /api/v1/*   : JSON API. Routes are "METHOD /pattern/{param}" with an
//                 auth level; one place enforces session cookie, CSRF and the
//                 forced-password-change gate (AGENTS.md §8).
// * /api/v1/ws  : WebSocket (auth via session cookie on the handshake).
// * /*          : static SPA from LittleFS, pre-gzipped assets served with
//                 Content-Encoding: gzip, immutable caching for hashed
//                 bundles, index.html fallback for client-side routes.
// Every response carries the security headers (CSP, nosniff, frame deny).
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "core/cJSON.h"
#include "esp_err.h"
#include "esp_http_server.h"
#include "lumen/web/auth_manager.hpp"
#include "lumen/web/web_pack.hpp"

namespace lumen
{

class WebSocketManager;

struct WebServerConfig
{
    uint16_t port = 80;
    int max_open_sockets = 10;
    std::size_t max_body = 16 * 1024; // default request-size limit
    const char *static_root = "/lfs/web";
};

enum class RouteAuth : uint8_t
{
    kPublic = 0,        // no session needed
    kPasswordChange,    // any valid session, even one that must change password
    kUser,              // valid session with the password already changed
};

// One API request as seen by a route handler.
struct ApiRequest
{
    httpd_req_t *raw = nullptr;
    int method = 0;                  // http_method (HTTP_GET, ...)
    std::string path;                // without query string, after /api/v1
    std::string query;               // raw query string ("" if none)
    std::vector<std::string> params; // {param} captures in order
    std::string body;
    std::string client_ip;
    std::string session_token;       // "" for public routes without cookie
    Session session;                 // valid when session_token non-empty

    // Parsed JSON body (owned by the request; nullptr if empty/invalid).
    cJSON *json();
    std::string query_param(const char *key) const;
    ~ApiRequest();

  private:
    cJSON *json_ = nullptr;
    bool parsed_ = false;
};

using ApiHandler = std::function<esp_err_t(ApiRequest &)>;

// Response helpers (envelope {success, data, error} — SPEC §17.1).
esp_err_t send_data(ApiRequest &req, cJSON *data, int status = 200); // takes ownership
esp_err_t send_ok(ApiRequest &req);                                   // data: null
esp_err_t send_error(ApiRequest &req, int status, const char *code, const std::string &message);
esp_err_t send_raw_json(httpd_req_t *req, int status, const std::string &body);

class WebServer
{
  public:
    WebServer(AuthManager &auth, const WebServerConfig &config = {});
    ~WebServer();

    esp_err_t start();
    esp_err_t stop();
    bool running() const { return server_ != nullptr; }
    httpd_handle_t handle() const { return server_; }

    // Register an API route. `pattern` is relative to /api/v1 and may use
    // {name} segments, e.g. "/lights/{id}/command". `max_body` overrides
    // the default request-size limit for upload routes.
    void route(int method, const char *pattern, RouteAuth auth, ApiHandler handler,
               std::size_t max_body = 0);
    // Upload route: auth/CSRF/size are checked as usual, but the body is NOT
    // buffered — the handler reads it from req.raw with httpd_req_recv()
    // (firmware images, web packs). `max_body` is the total size limit.
    void route_stream(int method, const char *pattern, RouteAuth auth, ApiHandler handler, std::size_t max_body);

    void set_ws_manager(WebSocketManager *ws) { ws_ = ws; }

    // Cookie helpers used by the auth routes.
    static void set_session_cookie(httpd_req_t *req, const std::string &token, int64_t max_age_s);
    static void clear_session_cookie(httpd_req_t *req);
    static std::string cookie_token(httpd_req_t *req);
    static std::string client_ip(httpd_req_t *req);

    uint32_t requests_served() const { return requests_; }
    WebPack &pack() { return pack_; }

  private:
    struct Route
    {
        int method;
        std::vector<std::string> segments;
        RouteAuth auth;
        ApiHandler handler;
        std::size_t max_body;
        bool stream = false;
    };

    static esp_err_t api_trampoline(httpd_req_t *req);
    static esp_err_t static_trampoline(httpd_req_t *req);
    static esp_err_t ws_trampoline(httpd_req_t *req);
    static esp_err_t ws_pre_handshake(httpd_req_t *req);
    static esp_err_t ws_post_handshake(httpd_req_t *req);
    static esp_err_t options_trampoline(httpd_req_t *req);

    esp_err_t handle_api(httpd_req_t *req);
    esp_err_t handle_static(httpd_req_t *req);
    bool serve_file(httpd_req_t *req, const std::string &path, bool is_index);

    AuthManager &auth_;
    WebServerConfig config_;
    httpd_handle_t server_ = nullptr;
    WebSocketManager *ws_ = nullptr;
    std::vector<Route> routes_;
    uint32_t requests_ = 0;
    WebPack pack_{"/lfs/web/web.pack"};
};

void add_security_headers(httpd_req_t *req);

} // namespace lumen
