// Lumen — multi-controller synchronisation over ESP-NOW (SPEC §12, Phase 11).
//
// Model: every light is owned by exactly one controller. Owners broadcast
// their lights' state (STATE, on change, coalesced); peers cache it. Another
// controller changes a remote light by sending a COMMAND addressed to the
// owner, which applies it through the normal enqueue path (origin
// "espnow:<peer>") and then broadcasts the resulting state. Commands are
// never forwarded and state is only sent by its owner, so loops cannot form.
// Network-wide scenes: SCENE(id) makes every controller activate its own
// scene with that id.
//
// Conflict rules: duplicates (msg id) and replays (per-sender seq) dropped;
// frames outside ±120 s dropped when both clocks are valid; a remote command
// older than the last LOCAL command on that light is rejected as stale (local
// physical input and local UI win). Frames are authenticated with an HMAC
// (network key in NVS, never exported).
//
// Threads: the Wi-Fi task's receive callback only copies frames into a
// bounded queue; the "espnow" task (prio 3, 6 KB) decodes, applies and sends.
#pragma once

#include <array>
#include <atomic>
#include <bitset>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include "esp_err.h"
#include "lumen/app/app_context.hpp"
#include "lumen/networking/espnow_protocol.h"

struct esp_now_recv_info; // esp_now.h (kept out of this header)

namespace lumen
{

struct EspNowSettings
{
    bool enabled = false;
    std::string network_id = "home";
};

struct RemoteLight
{
    std::string id;
    std::string name;
    std::string type;
    bool available = true;
    LightState state{};
};

struct Peer
{
    std::array<uint8_t, 6> mac{};
    std::string id;
    std::string name;
    std::string ip;
    std::string version;
    int8_t rssi = 0;
    uint32_t last_seen_ms = 0;
    bool online = false;
    std::vector<RemoteLight> lights; // bounded: kMaxRemoteLights
};

struct EspNowStats
{
    uint32_t tx = 0, tx_failed = 0, rx = 0, rx_queue_full = 0;
    uint32_t malformed = 0, version = 0, network = 0, auth = 0;
    uint32_t replay = 0, duplicate = 0, stale = 0, peers_full = 0;
    uint32_t commands_sent = 0, commands_applied = 0, commands_rejected = 0, scenes_applied = 0;
};

class EspNowManager
{
  public:
    static constexpr std::size_t kMaxPeers = 8;
    static constexpr std::size_t kMaxRemoteLights = 32;
    static constexpr uint32_t kHelloMs = 5000;
    static constexpr uint32_t kOfflineMs = 20000;

    explicit EspNowManager(AppContext &ctx) : ctx_(ctx) {}

    void load();
    esp_err_t start(); // creates the task once; (re)initialises ESP-NOW when enabled
    EspNowSettings settings() const;
    esp_err_t update(const EspNowSettings &s, const std::string *key, std::string &error);
    std::string key() const;      // authenticated UI only
    std::string regenerate_key();

    std::vector<Peer> peers() const;
    EspNowStats stats() const;
    bool running() const { return running_.load(); }
    std::string controller_id() const;

    // Remote control (API). `cmd` uses the HA/MQTT JSON fields.
    bool send_command(const std::string &peer_id, const std::string &light_id, const cJSON *cmd, std::string &error);
    bool send_scene(const std::string &scene_id);

    // Lighting listener (model lock held).
    void light_changed(std::size_t index, const std::string &origin);
    void lights_reconfigured() { resync_ = true; }

  private:
    struct Rx
    {
        uint8_t mac[6];
        int8_t rssi;
        uint8_t len;
        uint8_t data[espnow::kMaxFrame];
    };

    static void recv_cb(const ::esp_now_recv_info *info, const uint8_t *data, int len);
    static void task_entry(void *arg);
    void run();
    esp_err_t init_radio();
    void deinit_radio();
    bool send(espnow::Type type, const std::string &payload);
    void handle(const Rx &rx);
    void on_hello(const Rx &rx, const cJSON *p);
    void on_state(const Rx &rx, const cJSON *p);
    void on_command(const Rx &rx, const espnow::Frame &f, const cJSON *p);
    void on_scene(const cJSON *p);
    void send_hello();
    void send_states(bool all);
    std::string state_payload(std::size_t index);
    Peer *peer_for(const uint8_t mac[6], bool create);
    void check_offline(uint32_t now_ms);

    AppContext &ctx_;
    mutable std::mutex mutex_; // settings, peers, stats, key
    EspNowSettings settings_;
    std::string key_;
    std::vector<Peer> peers_;
    EspNowStats stats_;
    std::atomic<bool> running_{false};
    bool started_ = false;
    bool restart_ = false;
    void *queue_ = nullptr; // QueueHandle_t of Rx
    std::mutex send_mutex_;
    uint32_t seq_counter_ = 0;
    uint32_t boot_ = 0;
    espnow::ReplayFilter replay_;
    std::mutex dirty_mutex_;
    std::bitset<64> dirty_;
    std::array<int64_t, 64> last_local_unix_{}; // per light index: last non-ESP-NOW command
    bool resync_ = true;
    bool announce_ = false; // a new peer appeared: send all states once
};

} // namespace lumen
