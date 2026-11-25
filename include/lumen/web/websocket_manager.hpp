// Lumen WebSocket manager (SPEC §17.3, DESIGN §8.4).
//
// * Endpoint /api/v1/ws, authenticated on the handshake by session cookie.
// * Envelope: {"version":1,"event":"...","timestamp":<unix s or uptime s>,
//   "data":{...}}.
// * Bounded outbound queue. light_state_changed is COALESCED per light: the
//   manager only remembers *which* lights changed and renders their latest
//   state when it flushes, so a slider drag can never flood a slow client.
//   Other events use a small FIFO; when full the oldest telemetry event is
//   dropped and counted (never blocks the producer).
// * Flushes run in the httpd task via httpd_queue_work(); producers (the
//   lighting task, input service, ...) never touch sockets.
#pragma once

#include <array>
#include <bitset>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>

#include "esp_err.h"
#include "esp_http_server.h"

namespace lumen
{

class WebSocketManager
{
  public:
    static constexpr std::size_t kMaxClients = 6;
    static constexpr std::size_t kQueueSize = 24;
    static constexpr std::size_t kMaxLights = 64;

    // Renders the current state of light `index` (by channel slot) as the
    // JSON `data` object of a light_state_changed event; returns "" if gone.
    using LightRenderer = std::function<std::string(std::size_t index)>;
    // Re-validates a session token (sessions die on logout/password change).
    using SessionCheck = std::function<bool(const std::string &token)>;

    WebSocketManager() = default;

    void attach(httpd_handle_t server) { server_ = server; }
    void set_light_renderer(LightRenderer r) { render_light_ = std::move(r); }
    void set_session_check(SessionCheck c) { session_check_ = std::move(c); }

    // httpd callbacks (server task).
    esp_err_t on_open(httpd_req_t *req, const std::string &session_token);
    esp_err_t on_frame(httpd_req_t *req);

    // Producers (any task).
    void notify_light(std::size_t index);
    void broadcast(const char *event, const std::string &data_json, bool droppable = true);

    // Drop clients whose session ended (call after logout/password change).
    void revalidate_sessions();

    std::size_t client_count() const;
    uint32_t dropped() const { return dropped_; }
    uint32_t sent() const { return sent_; }

  private:
    struct Client
    {
        int fd = -1;
        std::string token;
    };
    struct Pending
    {
        std::string text;
        bool droppable = true;
    };

    static void flush_work(void *arg);
    void flush();
    void schedule_flush();
    void send_all(const std::string &text);
    std::string envelope(const char *event, const std::string &data_json) const;

    httpd_handle_t server_ = nullptr;
    mutable std::mutex mutex_;
    std::array<Client, kMaxClients> clients_{};
    std::bitset<kMaxLights> dirty_lights_;
    std::array<Pending, kQueueSize> queue_{};
    std::size_t q_head_ = 0;
    std::size_t q_count_ = 0;
    bool flush_scheduled_ = false;
    LightRenderer render_light_;
    SessionCheck session_check_;
    uint32_t dropped_ = 0;
    uint32_t sent_ = 0;
};

} // namespace lumen
