// Lumen — TCP/UDP control (SPEC §14).
//
// TCP (default port 8266): newline-delimited JSON, protocol v1.
//   -> {"v":1,"type":"auth","token":"<integration token>"}   (within 10 s)
//   -> {"v":1,"id":1,"type":"light","light_id":"x", <HA json fields>}
//   -> {"v":1,"id":2,"type":"group","group_id":"g", <HA json fields>}
//   -> {"v":1,"id":3,"type":"scene","scene_id":"s"}
//   -> {"v":1,"id":4,"type":"get"[,"light_id":"x"]}   -> state(s)
//   -> {"v":1,"id":5,"type":"ping"}                    -> pong (heartbeat)
//   <- {"v":1,"id":N,"ok":true|false[,"error":"..."]}
//   <- {"v":1,"type":"state","light_id":"x", <HA state>}   pushed on change
//   Max 4 clients, 1 KB lines, idle timeout 300 s.
// UDP (default port 8267):
//   discovery (always on when UDP is enabled): "LUMEN?" or
//   {"v":1,"type":"discover"} -> unicast {"v":1,"type":"announce",...}
//   control (OFF by default): HMAC-SHA256 frames (integration_protocol.h)
//   with ±30 s timestamps, per-sender sequence replay protection and a
//   20 packets/s rate limit. Optional state announcements (broadcast).
// The integration token lives in NVS (never in JSON/exports/logs).
#pragma once

#include <array>
#include <bitset>
#include <cstdint>
#include <mutex>
#include <string>

#include "lumen/app/app_context.hpp"
#include "lumen/networking/integration_protocol.h"

namespace lumen
{

struct ControlSettings
{
    bool tcp_enabled = false;
    uint16_t tcp_port = 8266;
    bool udp_enabled = true;        // discovery
    bool udp_control = false;       // authenticated control (off by default)
    bool udp_announce = false;      // broadcast state changes
    uint16_t udp_port = 8267;
};

struct ControlStatus
{
    uint8_t tcp_clients = 0;
    uint32_t tcp_commands = 0;
    uint32_t tcp_auth_failures = 0;
    uint32_t udp_commands = 0;
    uint32_t udp_rejected = 0;
    uint32_t discovery_replies = 0;
};

class ControlServers
{
  public:
    static constexpr std::size_t kMaxClients = 4;

    explicit ControlServers(AppContext &ctx) : ctx_(ctx) {}

    void load();
    void start(); // creates the "ctrl" task once
    ControlSettings settings() const;
    ControlStatus status() const;
    esp_err_t update(const ControlSettings &s, std::string &error);
    std::string token() const;          // for the authenticated UI only
    std::string regenerate_token();

    // Model lock held by caller (lighting task listener).
    void light_changed(std::size_t index)
    {
        std::lock_guard<std::mutex> l(dirty_mutex_);
        dirty_.set(index);
    }

  private:
    struct Client
    {
        int fd = -1;
        bool authed = false;
        uint32_t connected_ms = 0;
        uint32_t last_rx_ms = 0;
        integration::LineFramer framer{1024};
    };

    static void task_entry(void *arg);
    void run();
    void open_sockets();
    void close_sockets();
    void handle_line(Client &c, const std::string &line);
    void handle_udp(const uint8_t *data, std::size_t len, const void *from, unsigned from_len);
    bool execute(const cJSON *cmd, const std::string &origin, std::string &error, cJSON *reply);
    void send_line(Client &c, cJSON *obj); // takes ownership
    void push_states();

    AppContext &ctx_;
    mutable std::mutex mutex_;
    ControlSettings settings_;
    ControlStatus status_;
    std::string token_;
    bool restart_ = false;
    bool started_ = false;
    int listen_fd_ = -1;
    int udp_fd_ = -1;
    std::array<Client, kMaxClients> clients_{};
    std::mutex dirty_mutex_;
    std::bitset<64> dirty_;
    integration::ReplayGuard replay_;
    uint32_t rate_window_ms_ = 0;
    uint16_t rate_count_ = 0;
};

} // namespace lumen
