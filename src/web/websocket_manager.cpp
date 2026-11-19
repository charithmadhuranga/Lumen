// Lumen WebSocket manager implementation (stub)
#include "lumen/web/websocket_manager.hpp"

#include <cstring>
#include <string>

#include "esp_err.h"
#include "esp_log.h"
#include "esp_http_server.h"

static const char *TAG = "ws_manager";

namespace lumen
{

WebSocketManager::WebSocketManager(const WsConfig &config) : config_(config) {}

WebSocketManager::~WebSocketManager()
{
    // Clean up all clients
    for (size_t i = 0; i < client_count_; ++i) {
        if (clients_[i].fd >= 0) {
            httpd_sess_trigger_close(server_, clients_[i].fd);
        }
    }
}

esp_err_t WebSocketManager::init(EventBus *event_bus)
{
    event_bus_ = event_bus;
    ESP_LOGI(TAG, "WebSocket manager initialized (stub)");
    return ESP_OK;
}

esp_err_t WebSocketManager::handle_upgrade(httpd_req_t *req)
{
    ESP_LOGI(TAG, "WebSocket upgrade requested (stub - not fully implemented)");
    // For now, just return 404
    httpd_resp_set_status(req, "501 Not Implemented");
    httpd_resp_send(req, "WebSocket not yet implemented", -1);
    return ESP_OK;
}

esp_err_t WebSocketManager::broadcast(const char *event, const std::string &json_payload)
{
    (void)event;
    (void)json_payload;
    // Stub: no clients to broadcast to
    return ESP_OK;
}

esp_err_t WebSocketManager::send_to(const char *session_token, const char *event, const std::string &json_payload)
{
    (void)session_token;
    (void)event;
    (void)json_payload;
    return ESP_ERR_NOT_FOUND;
}

size_t WebSocketManager::client_count() const
{
    return client_count_;
}

void WebSocketManager::cleanup()
{
    // Stub
}

} // namespace lumen