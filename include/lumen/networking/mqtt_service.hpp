// Lumen — MQTT + Home Assistant discovery (SPEC §13).
//
// Topics (base = {prefix}/{controller_id}):
//   base/status                        "online"/"offline" (retained, LWT)
//   base/light/{id}/set  -> commands   base/light/{id}/state  (retained JSON)
//   base/group/{id}/set                base/scene/{id}/activate
//   base/event                         inputs, scenes, automation results
//   base/automation/{id}/status        last execution (retained)
//   base/diagnostics                   uptime, heap, RSSI (60 s)
// HA discovery: {ha_prefix}/light/{cid}/{light}/config (+ diagnostic
// sensors); configs of deleted lights are cleared (empty retained payload).
//
// Settings live in mqtt.json (no secrets); the password is in NVS. Event
// callbacks only enqueue lighting commands; state publishing is coalesced
// and done from the housekeeping task.
#pragma once

#include <bitset>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include "esp_err.h"
#include "lumen/app/app_context.hpp"

namespace lumen
{

struct MqttSettings
{
    bool enabled = false;
    std::string host;
    uint16_t port = 1883;
    bool tls = false;
    std::string username;
    std::string client_id;     // "" = controller id
    std::string topic_prefix = "lighting";
    std::string controller_id; // "" = hostname
    int qos = 0;
    int keepalive = 60;
    bool retain = true;
    bool ha_discovery = true;
    std::string ha_prefix = "homeassistant";
};

struct MqttStatus
{
    bool enabled = false;
    bool connected = false;
    std::string broker;
    std::string last_error;
    uint32_t published = 0;
    uint32_t received = 0;
    uint32_t rejected = 0;
    uint32_t reconnects = 0;
    uint32_t dropped = 0;
    bool has_password = false;
};

class MqttService
{
  public:
    explicit MqttService(AppContext &ctx) : ctx_(ctx) {}

    void load();             // mqtt.json + NVS password
    esp_err_t start();       // no-op when disabled / no network
    void stop();

    MqttSettings settings() const;
    MqttStatus status() const;
    // Validate, persist and restart. `password` null = keep current.
    esp_err_t update(const MqttSettings &s, const std::string *password, std::string &error);

    // Queue a message (bounded, non-blocking, any task). Sent by poll().
    bool publish(const std::string &topic, const std::string &payload, bool retain);
    std::string base() const;

    // Called with the model lock held (lighting task / API).
    void light_changed(std::size_t index) { std::lock_guard<std::mutex> l(dirty_mutex_); dirty_.set(index); }
    void lights_reconfigured()
    {
        rediscover_ = true;
        resync_all_ = true; // indices may have shifted
    }
    void event(const std::string &json);
    void automation_status(const std::string &id, const std::string &json);

    // Housekeeping task, every 250 ms.
    void poll(uint32_t now_ms);

  private:
    static void on_event(void *arg, const char *base, int32_t id, void *data);
    void handle_event(int32_t id, void *data);
    void handle_message(const std::string &topic, const std::string &payload);
    struct Outbound
    {
        std::string topic;
        std::string payload;
        bool retain = false;
    };
    // Direct write (esp_mqtt_client_publish). Never with the model lock held.
    bool send_now(const Outbound &m);
    void publish_discovery(std::vector<Outbound> &out);
    void publish_states(bool all, std::vector<Outbound> &out);
    void save_discovered(const std::vector<std::string> &ids);

    AppContext &ctx_;
    mutable std::mutex mutex_;
    MqttSettings settings_;
    std::string password_;
    MqttStatus status_;
    void *client_ = nullptr; // esp_mqtt_client_handle_t
    std::string uri_, lwt_topic_, client_id_;
    std::mutex dirty_mutex_;
    std::bitset<64> dirty_;
    bool rediscover_ = false;
    bool resync_all_ = false;
    uint32_t last_diag_ms_ = 0;
    std::vector<std::string> discovered_; // light ids with retained HA config
    // Inbox: the MQTT task only queues messages (never takes the model lock,
    // which would invert lock order with publishing). Drained by poll().
    struct Inbound
    {
        std::string topic;
        std::string payload;
    };
    static constexpr std::size_t kInbox = 16;
    std::vector<Inbound> inbox_;
    bool resubscribe_ = false;
    // esp-mqtt's enqueue() outbox drains ~1 message per task loop (about 1/s),
    // so poll() writes directly instead, outside the model lock.
    static constexpr std::size_t kAsync = 16;     // event/automation queue
    static constexpr std::size_t kDiscChunk = 8;  // discovery configs per poll
    static constexpr std::size_t kDiscIdle = static_cast<std::size_t>(-1);
    std::vector<Outbound> async_;                 // guarded by mutex_
    std::size_t disc_next_ = kDiscIdle;
};

} // namespace lumen
