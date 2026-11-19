// Lumen WebSocket manager — handles /api/v1/ws connections.
// SPEC §17.3, DESIGN §8.4.
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

#include "esp_err.h"
#include "esp_http_server.h"

namespace lumen
{

// Forward declaration
class EventBus;

// WebSocket message
struct WsMessage
{
    uint32_t version = 1;
    std::string event;
    int64_t timestamp = 0;
    std::string data; // JSON payload
};

// Configuration
struct WsConfig
{
    // Maximum message size (default: 4 KB)
    std::size_t max_message_size = 4096;

    // Outbound queue depth per connection (default: 32)
    std::size_t queue_depth = 32;

    // Ping interval in ms (default: 30 s)
    int64_t ping_interval_ms = 30000;

    // Slow client timeout (default: 5 s)
    int64_t slow_client_timeout_ms = 5000;
};

class WebSocketManager
{
  public:
    explicit WebSocketManager(const WsConfig &config = {});
    ~WebSocketManager();

    // Initialize with event bus for event fan-out
    esp_err_t init(EventBus *event_bus);

    // Handle HTTP upgrade request (called from web server)
    esp_err_t handle_upgrade(httpd_req_t *req);

    // Broadcast event to all connected clients
    esp_err_t broadcast(const char *event, const std::string &json_payload);

    // Send message to specific client (by session token)
    esp_err_t send_to(const char *session_token, const char *event, const std::string &json_payload);

    // Get number of connected clients
    size_t client_count() const;

    // Clean up stale connections
    void cleanup();

  private:
    WsConfig config_;
    EventBus *event_bus_ = nullptr;
    httpd_handle_t server_ = nullptr;

    struct Client
    {
        int fd = -1;
        std::string session_token;
        int64_t last_ping_ms = 0;
        int64_t last_activity_ms = 0;
    };
    // Simple fixed array for clients (max 8 concurrent)
    static constexpr size_t kMaxClients = 8;
    Client clients_[kMaxClients];
    size_t client_count_ = 0;

    esp_err_t handle_client_message(Client *client, const char *data, size_t len);
    esp_err_t send_frame(Client *client, const std::string &payload);
    void remove_client(Client *client);
};

} // namespace lumen