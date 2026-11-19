// Lumen web server — ESP-IDF HTTP server with static file serving from
// LittleFS. SPEC §16.1–16.3, §17, DESIGN §14.2.
//
// Features:
// - Static assets from LittleFS with pre-gzipped support (Content-Encoding: gzip)
// - SPA fallback to index.html for unknown non-API paths (DESIGN §14.2)
// - Strict CSP header (DESIGN §13)
// - Request size limits and connection limits
// - API routes mounted under /api/v1/
// - WebSocket upgrade at /api/v1/ws
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

#include "esp_err.h"
#include "esp_http_server.h"

namespace lumen
{

// Forward declaration for WebSocket manager
class WebSocketManager;

// Configuration for the web server
struct WebServerConfig
{
    // Maximum request body size in bytes (default: 64 KB)
    std::size_t max_request_body = 65536;

    // Maximum number of concurrent connections (default: 8)
    int max_connections = 8;

    // Base path for static assets in LittleFS (default: "/spiffs/web")
    const char *static_base_path = "/spiffs/web";

    // URI prefix for API routes (default: "/api/v1")
    const char *api_prefix = "/api/v1";

    // WebSocket URI (default: "/api/v1/ws")
    const char *ws_uri = "/api/v1/ws";

    // Enable CORS for development (default: false)
    bool enable_cors = false;
};

// HTTP request handler function type
using HttpHandler = std::function<esp_err_t(httpd_req_t *req)>;

// WebSocket message handler
using WsHandler = std::function<void(const std::string &payload)>;

// Main web server class
class WebServer
{
  public:
    explicit WebServer(const WebServerConfig &config = {});
    ~WebServer();

    // Start the HTTP server
    esp_err_t start();

    // Stop the HTTP server
    esp_err_t stop();

    // Register an HTTP handler for API routes
    // Path is relative to api_prefix (e.g., "/lights" → "/api/v1/lights")
    esp_err_t register_handler(const char *method, const char *path, HttpHandler handler);

    // Set WebSocket manager for handling /ws upgrades
    void set_ws_manager(WebSocketManager *ws_manager);

    // Get the underlying httpd_handle_t for advanced usage
    httpd_handle_t handle() const { return server_; }

    // Check if server is running
    bool running() const { return server_ != nullptr; }

  private:
    WebServerConfig config_;
    httpd_handle_t server_ = nullptr;
    WebSocketManager *ws_manager_ = nullptr;

    // Static URI handlers that forward to registered handlers
    static esp_err_t http_get_handler(httpd_req_t *req);
    static esp_err_t http_post_handler(httpd_req_t *req);
    static esp_err_t http_put_handler(httpd_req_t *req);
    static esp_err_t http_delete_handler(httpd_req_t *req);
    static esp_err_t http_options_handler(httpd_req_t *req);
    static esp_err_t ws_upgrade_handler(httpd_req_t *req);

    // Instance method handlers
    esp_err_t handle_get(httpd_req_t *req);
    esp_err_t handle_post(httpd_req_t *req);
    esp_err_t handle_put(httpd_req_t *req);
    esp_err_t handle_delete(httpd_req_t *req);

    // Static file serving
    esp_err_t serve_static(httpd_req_t *req);
    esp_err_t serve_index(httpd_req_t *req);

    // CORS headers
    void add_cors_headers(httpd_req_t *req) const;

    // CSP header
    void add_csp_header(httpd_req_t *req) const;
};

} // namespace lumen