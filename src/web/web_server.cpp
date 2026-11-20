// Lumen web server implementation
#include "lumen/web/web_server.hpp"
#include "lumen/web/websocket_manager.hpp"

#include <cstring>
#include <string>
#include <vector>

#include "esp_err.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_spiffs.h"

#include "core/cJSON.h"
#include "lumen/core/json.hpp"

static const char *TAG = "web_server";

// Forward declarations for helper functions
namespace lumen
{
esp_err_t read_request_body(httpd_req_t *req, std::string &out, std::size_t max_size);
esp_err_t send_json_response(httpd_req_t *req, int status, const std::string &json);
esp_err_t send_error_response(httpd_req_t *req, int status, const char *code, const char *message);
} // namespace lumen

namespace lumen
{

namespace
{

// MIME type mapping
struct MimeEntry
{
    const char *ext;
    const char *mime;
};

constexpr MimeEntry kMimeTypes[] = {
    {".html", "text/html; charset=utf-8"},
    {".css", "text/css; charset=utf-8"},
    {".js", "application/javascript; charset=utf-8"},
    {".json", "application/json; charset=utf-8"},
    {".svg", "image/svg+xml; charset=utf-8"},
    {".ico", "image/x-icon"},
    {".png", "image/png"},
    {".woff", "font/woff"},
    {".woff2", "font/woff2"},
    {".txt", "text/plain; charset=utf-8"},
};

const char *get_mime_type(const char *path)
{
    const char *dot = std::strrchr(path, '.');
    if (!dot) {
        return "application/octet-stream";
    }
    for (const auto &entry : kMimeTypes) {
        if (std::strcmp(dot, entry.ext) == 0) {
            return entry.mime;
        }
    }
    return "application/octet-stream";
}

// Check if path should be served as gzipped
bool is_gzipped_request(httpd_req_t *req)
{
    size_t len = 0;
    esp_err_t err = httpd_req_get_hdr_value_str(req, "Accept-Encoding", nullptr, len);
    if (err != ESP_OK || len == 0) {
        return false;
    }
    std::string encoding(len, '\0');
    err = httpd_req_get_hdr_value_str(req, "Accept-Encoding", encoding.data(), len);
    if (err != ESP_OK) {
        return false;
    }
    return encoding.find("gzip") != std::string::npos;
}

} // namespace

WebServer::WebServer(const WebServerConfig &config) : config_(config) {}

WebServer::~WebServer()
{
    stop();
}

esp_err_t WebServer::start()
{
    if (server_) {
        return ESP_ERR_INVALID_STATE;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_open_sockets = config_.max_connections;
    config.max_uri_handlers = 32;
    config.max_resp_headers = 16;
    config.stack_size = 8192;
    config.recv_wait_timeout = 5;
    config.send_wait_timeout = 5;

    esp_err_t err = httpd_start(&server_, &config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start failed: %s", esp_err_to_name(err));
        return err;
    }

    // Register static file handler (catch-all for non-API paths)
    httpd_uri_t static_get = {
        .uri = "/*",
        .method = HTTP_GET,
        .handler = http_get_handler,
        .user_ctx = this,
    };
    err = httpd_register_uri_handler(server_, &static_get);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "static GET register failed: %s", esp_err_to_name(err));
        httpd_stop(server_);
        server_ = nullptr;
        return err;
    }

    // Register OPTIONS for CORS preflight
    httpd_uri_t options = {
        .uri = "/*",
        .method = HTTP_OPTIONS,
        .handler = http_options_handler,
        .user_ctx = this,
    };
    httpd_register_uri_handler(server_, &options);

    // WebSocket upgrade (handled by ws_upgrade_handler)
    httpd_uri_t ws = {
        .uri = config_.ws_uri,
        .method = HTTP_GET,
        .handler = ws_upgrade_handler,
        .user_ctx = this,
    };
    httpd_register_uri_handler(server_, &ws);

    ESP_LOGI(TAG, "Web server started on port %d", config.server_port);
    return ESP_OK;
}

esp_err_t WebServer::stop()
{
    if (!server_) {
        return ESP_OK;
    }
    esp_err_t err = httpd_stop(server_);
    server_ = nullptr;
    return err;
}

esp_err_t WebServer::api_dispatch(httpd_req_t *req)
{
    WebServer *self = static_cast<WebServer *>(req->user_ctx);
    if (self == nullptr) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no ctx");
        return ESP_FAIL;
    }

    std::vector<HandlerEntry> *handlers = nullptr;
    switch (req->method) {
        case HTTP_GET: handlers = &self->get_handlers_; break;
        case HTTP_POST: handlers = &self->post_handlers_; break;
        case HTTP_PUT: handlers = &self->put_handlers_; break;
        case HTTP_DELETE: handlers = &self->delete_handlers_; break;
        default: break;
    }
    if (handlers != nullptr) {
        for (const auto &entry : *handlers) {
            if (entry.path == req->uri) {
                return entry.handler(req);
            }
        }
    }

    httpd_resp_set_status(req, "404 Not Found");
    return send_error_response(req, 404, "NOT_FOUND", "API endpoint not found");
}

esp_err_t WebServer::register_handler(const char *method, const char *path, HttpHandler handler)
{
    if (!server_) {
        return ESP_ERR_INVALID_STATE;
    }

    // Build full path: api_prefix + path
    std::string full_path = config_.api_prefix;
    full_path += path;

    httpd_method_t http_method;
    std::vector<HandlerEntry> *handlers = nullptr;
    if (std::strcmp(method, "GET") == 0) {
        http_method = HTTP_GET;
        handlers = &get_handlers_;
    } else if (std::strcmp(method, "POST") == 0) {
        http_method = HTTP_POST;
        handlers = &post_handlers_;
    } else if (std::strcmp(method, "PUT") == 0) {
        http_method = HTTP_PUT;
        handlers = &put_handlers_;
    } else if (std::strcmp(method, "DELETE") == 0) {
        http_method = HTTP_DELETE;
        handlers = &delete_handlers_;
    } else {
        return ESP_ERR_INVALID_ARG;
    }

    // Replace an existing handler for the same path.
    for (auto &entry : *handlers) {
        if (entry.path == full_path) {
            entry.handler = std::move(handler);
            return ESP_OK;
        }
    }

    // Persist the URI string (httpd keeps the pointer) and register a real
    // httpd handler that dispatches through the registry.
    if (uri_storage_.capacity() < 64) {
        uri_storage_.reserve(64);
    }
    uri_storage_.push_back(full_path);

    httpd_uri_t uri = {};
    uri.uri = uri_storage_.back().c_str();
    uri.method = http_method;
    uri.handler = api_dispatch;
    uri.user_ctx = this;

    esp_err_t err = httpd_register_uri_handler(server_, &uri);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "register %s %s failed: %s", method, full_path.c_str(),
                 esp_err_to_name(err));
        return err;
    }

    handlers->push_back({full_path, std::move(handler)});
    return ESP_OK;
}

void WebServer::set_ws_manager(WebSocketManager *ws_manager)
{
    ws_manager_ = ws_manager;
}

esp_err_t WebServer::http_get_handler(httpd_req_t *req)
{
    WebServer *self = static_cast<WebServer *>(req->user_ctx);
    return self->handle_get(req);
}

esp_err_t WebServer::http_post_handler(httpd_req_t *req)
{
    WebServer *self = static_cast<WebServer *>(req->user_ctx);
    return self->handle_post(req);
}

esp_err_t WebServer::http_put_handler(httpd_req_t *req)
{
    WebServer *self = static_cast<WebServer *>(req->user_ctx);
    return self->handle_put(req);
}

esp_err_t WebServer::http_delete_handler(httpd_req_t *req)
{
    WebServer *self = static_cast<WebServer *>(req->user_ctx);
    return self->handle_delete(req);
}

esp_err_t WebServer::http_options_handler(httpd_req_t *req)
{
    WebServer *self = static_cast<WebServer *>(req->user_ctx);
    self->add_cors_headers(req);
    httpd_resp_set_status(req, "204 No Content");
    httpd_resp_send(req, nullptr, 0);
    return ESP_OK;
}

esp_err_t WebServer::ws_upgrade_handler(httpd_req_t *req)
{
    WebServer *self = static_cast<WebServer *>(req->user_ctx);
    if (self->ws_manager_) {
        return self->ws_manager_->handle_upgrade(req);
    }
    httpd_resp_set_status(req, "404 Not Found");
    httpd_resp_send(req, nullptr, 0);
    return ESP_OK;
}

esp_err_t WebServer::handle_get(httpd_req_t *req)
{
    add_cors_headers(req);
    add_csp_header(req);

    const char *uri = req->uri;

    // Check if it's an API route
    if (std::strncmp(uri, config_.api_prefix, std::strlen(config_.api_prefix)) == 0) {
        // API route - check registered handlers
        static std::vector<std::pair<std::string, HttpHandler>> get_handlers;
        for (const auto &entry : get_handlers) {
            if (entry.first == uri) {
                return entry.second(req);
            }
        }
        httpd_resp_set_status(req, "404 Not Found");
        return send_error_response(req, 404, "NOT_FOUND", "API endpoint not found");
    }

    // Static file or SPA fallback
    if (std::strstr(uri, "/api/") == uri) {
        // API path but no handler
        httpd_resp_set_status(req, "404 Not Found");
        return send_error_response(req, 404, "NOT_FOUND", "API endpoint not found");
    }

    // Static file serving
    esp_err_t err = serve_static(req);
    if (err == ESP_OK) {
        return ESP_OK;
    }

    // SPA fallback to index.html
    return serve_index(req);
}

esp_err_t WebServer::handle_post(httpd_req_t *req)
{
    add_cors_headers(req);
    add_csp_header(req);

    const char *uri = req->uri;
    if (std::strncmp(uri, config_.api_prefix, std::strlen(config_.api_prefix)) != 0) {
        httpd_resp_set_status(req, "404 Not Found");
        return send_error_response(req, 404, "NOT_FOUND", "API endpoint not found");
    }

    static std::vector<std::pair<std::string, HttpHandler>> post_handlers;
    for (const auto &entry : post_handlers) {
        if (entry.first == uri) {
            return entry.second(req);
        }
    }
    httpd_resp_set_status(req, "404 Not Found");
    return send_error_response(req, 404, "NOT_FOUND", "API endpoint not found");
}

esp_err_t WebServer::handle_put(httpd_req_t *req)
{
    add_cors_headers(req);
    add_csp_header(req);

    const char *uri = req->uri;
    if (std::strncmp(uri, config_.api_prefix, std::strlen(config_.api_prefix)) != 0) {
        httpd_resp_set_status(req, "404 Not Found");
        return send_error_response(req, 404, "NOT_FOUND", "API endpoint not found");
    }
    // PUT handlers would be registered similarly
    httpd_resp_set_status(req, "404 Not Found");
    return send_error_response(req, 404, "NOT_FOUND", "API endpoint not found");
}

esp_err_t WebServer::handle_delete(httpd_req_t *req)
{
    add_cors_headers(req);
    add_csp_header(req);

    const char *uri = req->uri;
    if (std::strncmp(uri, config_.api_prefix, std::strlen(config_.api_prefix)) != 0) {
        httpd_resp_set_status(req, "404 Not Found");
        return send_error_response(req, 404, "NOT_FOUND", "API endpoint not found");
    }
    httpd_resp_set_status(req, "404 Not Found");
    return send_error_response(req, 404, "NOT_FOUND", "API endpoint not found");
}

esp_err_t WebServer::serve_static(httpd_req_t *req)
{
    const char *uri = req->uri;
    std::string filepath = config_.static_base_path;
    filepath += uri;

    // Check for gzipped version first
    bool gzip_ok = is_gzipped_request(req);
    if (gzip_ok) {
        std::string gz_path = filepath + ".gz";
        FILE *f = std::fopen(gz_path.c_str(), "rb");
        if (f) {
            std::fclose(f);
            httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
            filepath = gz_path;
        }
    }

    FILE *f = std::fopen(filepath.c_str(), "rb");
    if (!f) {
        return ESP_ERR_NOT_FOUND;
    }

    // Get file size
    std::fseek(f, 0, SEEK_END);
    long fsize = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);

    if (fsize < 0) {
        std::fclose(f);
        return ESP_ERR_NOT_FOUND;
    }

    const char *mime = get_mime_type(uri);
    httpd_resp_set_type(req, mime);
    httpd_resp_set_hdr(req, "Cache-Control", "public, max-age=31536000, immutable");

    // Send file in chunks
    constexpr size_t kChunkSize = 4096;
    std::vector<char> buffer(kChunkSize);
    size_t remaining = static_cast<size_t>(fsize);

    while (remaining > 0) {
        size_t to_read = std::min(kChunkSize, remaining);
        size_t read = std::fread(buffer.data(), 1, to_read, f);
        if (read == 0) break;

        esp_err_t err = httpd_resp_send_chunk(req, buffer.data(), static_cast<int>(read));
        if (err != ESP_OK) {
            std::fclose(f);
            return err;
        }
        remaining -= read;
    }

    std::fclose(f);
    return httpd_resp_send_chunk(req, nullptr, 0);
}

esp_err_t WebServer::serve_index(httpd_req_t *req)
{
    std::string index_path = config_.static_base_path;
    index_path += "/index.html";

    FILE *f = std::fopen(index_path.c_str(), "rb");
    if (!f) {
        httpd_resp_set_status(req, "404 Not Found");
        return send_error_response(req, 404, "NOT_FOUND", "index.html not found");
    }

    std::fseek(f, 0, SEEK_END);
    long fsize = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);

    if (fsize < 0) {
        std::fclose(f);
        httpd_resp_set_status(req, "404 Not Found");
        return send_error_response(req, 404, "NOT_FOUND", "index.html not found");
    }

    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache, no-store, must-revalidate");

    constexpr size_t kChunkSize = 4096;
    std::vector<char> buffer(kChunkSize);
    size_t remaining = static_cast<size_t>(fsize);

    while (remaining > 0) {
        size_t to_read = std::min(kChunkSize, remaining);
        size_t read = std::fread(buffer.data(), 1, to_read, f);
        if (read == 0) break;

        esp_err_t err = httpd_resp_send_chunk(req, buffer.data(), static_cast<int>(read));
        if (err != ESP_OK) {
            std::fclose(f);
            return err;
        }
        remaining -= read;
    }

    std::fclose(f);
    return httpd_resp_send_chunk(req, nullptr, 0);
}

void WebServer::add_cors_headers(httpd_req_t *req) const
{
    if (!config_.enable_cors) {
        return;
    }
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "Content-Type, Authorization, X-CSRF-Token");
    httpd_resp_set_hdr(req, "Access-Control-Max-Age", "86400");
}

void WebServer::add_csp_header(httpd_req_t *req) const
{
    // Strict CSP per DESIGN §13
    const char *csp = "default-src 'self'; "
                      "script-src 'self'; "
                      "style-src 'self' 'unsafe-inline'; "
                      "img-src 'self' data:; "
                      "font-src 'self'; "
                      "connect-src 'self' ws: wss:; "
                      "frame-ancestors 'none'; "
                      "base-uri 'self'; "
                      "form-action 'self'";
    httpd_resp_set_hdr(req, "Content-Security-Policy", csp);
}

} // namespace lumen

// Forward declarations for helper functions
namespace lumen
{
esp_err_t read_request_body(httpd_req_t *req, std::string &out, std::size_t max_size);
esp_err_t send_json_response(httpd_req_t *req, int status, const std::string &json);
esp_err_t send_error_response(httpd_req_t *req, int status, const char *code, const char *message);
} // namespace lumen

// Helper implementations
namespace lumen
{

esp_err_t read_request_body(httpd_req_t *req, std::string &out, std::size_t max_size)
{
    out.clear();
    size_t remaining = req->content_len;
    if (remaining == 0) {
        return ESP_OK;
    }
    if (remaining > max_size) {
        return ESP_ERR_INVALID_SIZE;
    }

    out.reserve(remaining);
    std::vector<char> buffer(1024);
    while (remaining > 0) {
        size_t to_read = std::min(buffer.size(), remaining);
        int ret = httpd_req_recv(req, buffer.data(), static_cast<int>(to_read));
        if (ret <= 0) {
            return ESP_FAIL;
        }
        out.append(buffer.data(), static_cast<size_t>(ret));
        remaining -= static_cast<size_t>(ret);
    }
    return ESP_OK;
}

esp_err_t send_json_response(httpd_req_t *req, int status, const std::string &json)
{
    httpd_resp_set_status(req, status == 200 ? "200 OK" : "500 Internal Server Error");
    httpd_resp_set_type(req, "application/json; charset=utf-8");
    return httpd_resp_send(req, json.c_str(), static_cast<int>(json.size()));
}

esp_err_t send_error_response(httpd_req_t *req, int status, const char *code, const char *message)
{
    // Build error envelope: {success: false, error: {code, message}}
    std::string json = "{\"success\":false,\"error\":{\"code\":\"";
    json += code;
    json += "\",\"message\":\"";
    json += message;
    json += "\"}}";

    const char *status_str = "500 Internal Server Error";
    switch (status) {
        case 400: status_str = "400 Bad Request"; break;
        case 401: status_str = "401 Unauthorized"; break;
        case 403: status_str = "403 Forbidden"; break;
        case 404: status_str = "404 Not Found"; break;
        case 409: status_str = "409 Conflict"; break;
        case 429: status_str = "429 Too Many Requests"; break;
    }
    httpd_resp_set_status(req, status_str);
    httpd_resp_set_type(req, "application/json; charset=utf-8");
    return httpd_resp_send(req, json.c_str(), static_cast<int>(json.size()));
}

} // namespace lumen