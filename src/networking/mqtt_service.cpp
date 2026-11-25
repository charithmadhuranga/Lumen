// Lumen — MQTT + Home Assistant discovery. See mqtt_service.hpp
#include "lumen/networking/mqtt_service.hpp"

#include <algorithm>
#include <cstring>

#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "lumen/core/model_lock.hpp"
#include "lumen/networking/integration_protocol.h"
#include "lumen/version.hpp"
#include "lumen/web/api_util.hpp"
#include "mqtt_client.h"
#include "nvs.h"

namespace lumen
{

namespace
{
const char *TAG = "mqtt";
constexpr const char *kDoc = "mqtt.json";
constexpr std::size_t kMaxPayload = 2048;

esp_mqtt_client_handle_t handle(void *p)
{
    return static_cast<esp_mqtt_client_handle_t>(p);
}

std::string nvs_get(const char *key)
{
    nvs_handle_t h;
    if (nvs_open("mqtt", NVS_READONLY, &h) != ESP_OK) return {};
    std::size_t len = 0;
    std::string out;
    if (nvs_get_str(h, key, nullptr, &len) == ESP_OK && len > 0 && len <= 128) {
        out.resize(len);
        nvs_get_str(h, key, &out[0], &len);
        out.resize(len - 1);
    }
    nvs_close(h);
    return out;
}

bool valid_prefix(const std::string &s)
{
    if (s.empty() || s.size() > 32) return false;
    for (char c : s) {
        if (c == '#' || c == '+' || c == ' ' || c == '/') return false;
    }
    return true;
}
} // namespace

std::string MqttService::base() const
{
    return settings_.topic_prefix + "/" + (settings_.controller_id.empty() ? ctx_.network->status().hostname : settings_.controller_id);
}

void MqttService::load()
{
    DocConfig doc{kDoc, 1, migrate_additive, nullptr, nullptr};
    ctx_.config->register_document(doc);
    std::string text;
    MqttSettings s;
    if (ctx_.config->load_document(kDoc, text) == ConfigStatus::kOk) {
        cJSON *o = cJSON_Parse(text.c_str());
        if (o != nullptr) {
            api::get_bool(o, "enabled", s.enabled);
            api::get_string(o, "host", s.host, 128);
            double v = 0;
            if (api::get_number(o, "port", v)) s.port = static_cast<uint16_t>(v);
            api::get_bool(o, "tls", s.tls);
            api::get_string(o, "username", s.username, 64);
            api::get_string(o, "clientId", s.client_id, 64);
            api::get_string(o, "topicPrefix", s.topic_prefix, 32);
            api::get_string(o, "controllerId", s.controller_id, 32);
            if (api::get_number(o, "qos", v)) s.qos = static_cast<int>(v);
            if (api::get_number(o, "keepalive", v)) s.keepalive = static_cast<int>(v);
            api::get_bool(o, "retain", s.retain);
            api::get_bool(o, "haDiscovery", s.ha_discovery);
            api::get_string(o, "haPrefix", s.ha_prefix, 32);
            api::get_string_array(o, "discovered", discovered_, 64);
            cJSON_Delete(o);
        }
    }
    std::lock_guard<std::recursive_mutex> lock(model_mutex());
    std::lock_guard<std::mutex> l(mutex_);
    settings_ = s;
    password_ = nvs_get("pass");
    status_.enabled = s.enabled;
    status_.has_password = !password_.empty();
}

MqttSettings MqttService::settings() const
{
    std::lock_guard<std::mutex> l(mutex_);
    return settings_;
}

MqttStatus MqttService::status() const
{
    std::lock_guard<std::mutex> l(mutex_);
    return status_;
}

esp_err_t MqttService::start()
{
    MqttSettings s;
    {
        std::lock_guard<std::mutex> l(mutex_);
        s = settings_;
        status_.enabled = s.enabled;
        status_.connected = false;
    }
    if (!s.enabled || s.host.empty()) return ESP_OK;

    const std::string cid = s.controller_id.empty() ? ctx_.network->status().hostname : s.controller_id;
    uri_ = std::string(s.tls ? "mqtts://" : "mqtt://") + s.host + ":" + std::to_string(s.port);
    lwt_topic_ = s.topic_prefix + "/" + cid + "/status";
    client_id_ = s.client_id.empty() ? cid : s.client_id;

    esp_mqtt_client_config_t cfg{};
    cfg.broker.address.uri = uri_.c_str();
    if (s.tls) cfg.broker.verification.crt_bundle_attach = esp_crt_bundle_attach;
    cfg.credentials.client_id = client_id_.c_str();
    if (!s.username.empty()) cfg.credentials.username = settings_.username.c_str();
    if (!password_.empty()) cfg.credentials.authentication.password = password_.c_str();
    cfg.session.last_will.topic = lwt_topic_.c_str();
    cfg.session.last_will.msg = "offline";
    cfg.session.last_will.qos = 1;
    cfg.session.last_will.retain = 1;
    cfg.session.keepalive = s.keepalive;
    cfg.network.reconnect_timeout_ms = 5000;
    cfg.network.timeout_ms = 2000; // < task WDT (5 s): housekeeping publishes
    cfg.buffer.size = 2048;
    cfg.buffer.out_size = 2048;
    cfg.task.stack_size = 6144;
    cfg.outbox.limit = 16 * 1024; // bounded outbox

    esp_mqtt_client_handle_t c = esp_mqtt_client_init(&cfg);
    if (c == nullptr) {
        std::lock_guard<std::mutex> l(mutex_);
        status_.last_error = "client init failed";
        return ESP_FAIL;
    }
    esp_mqtt_client_register_event(c, MQTT_EVENT_ANY, reinterpret_cast<esp_event_handler_t>(&MqttService::on_event), this);
    const esp_err_t err = esp_mqtt_client_start(c);
    {
        std::lock_guard<std::mutex> l(mutex_);
        client_ = c;
        status_.broker = uri_;
        if (err != ESP_OK) status_.last_error = esp_err_to_name(err);
    }
    ESP_LOGI(TAG, "connecting to %s as %s", uri_.c_str(), client_id_.c_str());
    return err;
}

void MqttService::stop()
{
    void *c = nullptr;
    {
        std::lock_guard<std::mutex> l(mutex_);
        c = client_;
        client_ = nullptr;
        status_.connected = false;
    }
    if (c != nullptr) {
        esp_mqtt_client_publish(handle(c), lwt_topic_.c_str(), "offline", 0, 1, 1);
        esp_mqtt_client_stop(handle(c));
        esp_mqtt_client_destroy(handle(c));
    }
}

esp_err_t MqttService::update(const MqttSettings &s, const std::string *password, std::string &error)
{
    if (s.enabled && (s.host.empty() || s.host.size() > 128)) {
        error = "Broker host is required";
        return ESP_ERR_INVALID_ARG;
    }
    if (s.port == 0 || s.qos < 0 || s.qos > 1 || s.keepalive < 10 || s.keepalive > 3600) {
        error = "port must be 1-65535, qos 0-1, keepalive 10-3600 s";
        return ESP_ERR_INVALID_ARG;
    }
    if (!valid_prefix(s.topic_prefix) || !valid_prefix(s.ha_prefix) ||
        (!s.controller_id.empty() && !valid_prefix(s.controller_id))) {
        error = "Prefixes and controller id must be 1-32 characters without '/', '+', '#' or spaces";
        return ESP_ERR_INVALID_ARG;
    }
    if (password != nullptr && password->size() > 128) {
        error = "Password too long";
        return ESP_ERR_INVALID_ARG;
    }
    // Changing the identity orphans retained discovery: clear it first.
    const MqttSettings old = settings();
    if (old.ha_discovery && (old.controller_id != s.controller_id || old.ha_prefix != s.ha_prefix ||
                             old.topic_prefix != s.topic_prefix || !s.ha_discovery || !s.enabled)) {
        integration::DiscoveryContext dc;
        dc.discovery_prefix = old.ha_prefix;
        dc.controller_id = old.controller_id.empty() ? ctx_.network->status().hostname : old.controller_id;
        for (const auto &id : discovered_) send_now(Outbound{integration::discovery_topic(dc, id), "", true});
        const std::string sensor_base = old.ha_prefix + "/sensor/" + integration::topic_safe(dc.controller_id) + "/";
        for (const char *key : {"uptime", "free_heap", "rssi"}) send_now(Outbound{sensor_base + key + "/config", "", true});
        discovered_.clear();
    }
    stop();
    cJSON *o = cJSON_CreateObject();
    cJSON_AddBoolToObject(o, "enabled", s.enabled);
    cJSON_AddStringToObject(o, "host", s.host.c_str());
    cJSON_AddNumberToObject(o, "port", s.port);
    cJSON_AddBoolToObject(o, "tls", s.tls);
    cJSON_AddStringToObject(o, "username", s.username.c_str());
    cJSON_AddStringToObject(o, "clientId", s.client_id.c_str());
    cJSON_AddStringToObject(o, "topicPrefix", s.topic_prefix.c_str());
    cJSON_AddStringToObject(o, "controllerId", s.controller_id.c_str());
    cJSON_AddNumberToObject(o, "qos", s.qos);
    cJSON_AddNumberToObject(o, "keepalive", s.keepalive);
    cJSON_AddBoolToObject(o, "retain", s.retain);
    cJSON_AddBoolToObject(o, "haDiscovery", s.ha_discovery);
    cJSON_AddStringToObject(o, "haPrefix", s.ha_prefix.c_str());
    cJSON_AddItemToObject(o, "discovered", api::string_array(discovered_));
    const std::string text = api::to_string(o);
    cJSON_Delete(o);
    {
        ModelLock lock(model_mutex());
        if (ctx_.config->save_document(kDoc, text) != ConfigStatus::kOk) {
            error = "Could not save settings";
            return ESP_FAIL;
        }
    }
    if (password != nullptr) {
        nvs_handle_t h;
        if (nvs_open("mqtt", NVS_READWRITE, &h) == ESP_OK) {
            nvs_set_str(h, "pass", password->c_str());
            nvs_commit(h);
            nvs_close(h);
        }
    }
    load();
    {
        std::lock_guard<std::mutex> l(mutex_);
        status_.last_error.clear();
    }
    return start();
}

bool MqttService::publish(const std::string &topic, const std::string &payload, bool retain)
{
    std::lock_guard<std::mutex> l(mutex_);
    if (client_ == nullptr || !status_.connected) return false;
    if (async_.size() >= kAsync) {
        status_.dropped++;
        return false;
    }
    async_.push_back(Outbound{topic, payload, retain});
    return true;
}

bool MqttService::send_now(const Outbound &m)
{
    void *c = nullptr;
    int qos = 0;
    {
        // Never call into esp-mqtt while holding mutex_: the MQTT task takes
        // mutex_ from its event handler while holding its own client lock.
        std::lock_guard<std::mutex> l(mutex_);
        if (client_ == nullptr || !status_.connected) return false;
        c = client_;
        qos = settings_.qos;
    }
    const int id = esp_mqtt_client_publish(handle(c), m.topic.c_str(), m.payload.data(),
                                           static_cast<int>(m.payload.size()), qos, m.retain ? 1 : 0);
    std::lock_guard<std::mutex> l(mutex_);
    if (id >= 0) {
        status_.published++;
    } else {
        status_.dropped++;
    }
    return id >= 0;
}

void MqttService::on_event(void *arg, const char *, int32_t id, void *data)
{
    static_cast<MqttService *>(arg)->handle_event(id, data);
}

void MqttService::handle_event(int32_t id, void *data)
{
    auto *ev = static_cast<esp_mqtt_event_handle_t>(data);
    switch (static_cast<esp_mqtt_event_id_t>(id)) {
    case MQTT_EVENT_CONNECTED: {
        std::string b;
        {
            std::lock_guard<std::mutex> l(mutex_);
            status_.connected = true;
            status_.last_error.clear();
            b = base();
        }
        ESP_LOGI(TAG, "connected; base topic %s", b.c_str());
        esp_mqtt_client_publish(ev->client, lwt_topic_.c_str(), "online", 0, 1, 1);
        esp_mqtt_client_subscribe(ev->client, (b + "/light/+/set").c_str(), 1);
        esp_mqtt_client_subscribe(ev->client, (b + "/group/+/set").c_str(), 1);
        esp_mqtt_client_subscribe(ev->client, (b + "/scene/+/activate").c_str(), 1);
        resubscribe_ = true; // automation trigger topics (needs the model: done in poll)
        rediscover_ = true;
        resync_all_ = true;
        break;
    }
    case MQTT_EVENT_DISCONNECTED: {
        std::lock_guard<std::mutex> l(mutex_);
        if (status_.connected) status_.reconnects++;
        status_.connected = false;
        break;
    }
    case MQTT_EVENT_ERROR: {
        std::lock_guard<std::mutex> l(mutex_);
        if (ev->error_handle != nullptr && ev->error_handle->error_type == MQTT_ERROR_TYPE_CONNECTION_REFUSED) {
            status_.last_error = "Broker refused the connection (check username/password)";
        } else if (ev->error_handle != nullptr && ev->error_handle->esp_tls_last_esp_err != 0) {
            status_.last_error = std::string("TLS/transport error: ") + esp_err_to_name(ev->error_handle->esp_tls_last_esp_err);
        } else {
            status_.last_error = "Cannot reach the broker";
        }
        break;
    }
    case MQTT_EVENT_DATA:
        if (ev->total_data_len > static_cast<int>(kMaxPayload) || ev->current_data_offset != 0 ||
            ev->data_len != ev->total_data_len) {
            std::lock_guard<std::mutex> l(mutex_);
            status_.rejected++; // fragmented / oversized: bounded buffers only
            break;
        }
        {
            std::lock_guard<std::mutex> l(mutex_);
            if (inbox_.size() >= kInbox) {
                status_.rejected++; // bounded: drop when the controller is busy
            } else {
                inbox_.push_back({std::string(ev->topic, static_cast<std::size_t>(ev->topic_len)),
                                  std::string(ev->data, static_cast<std::size_t>(ev->data_len))});
            }
        }
        break;
    default:
        break;
    }
}

void MqttService::handle_message(const std::string &topic, const std::string &payload)
{
    {
        std::lock_guard<std::mutex> l(mutex_);
        status_.received++;
    }
    const std::string b = base() + "/";
    auto reject = [this](const char *why, const std::string &t) {
        std::lock_guard<std::mutex> l(mutex_);
        status_.rejected++;
        ESP_LOGW(TAG, "rejected %s: %s", t.c_str(), why);
    };
    if (topic.rfind(b, 0) != 0) {
        // Foreign topic: only automation triggers subscribe to these.
        AutoEvent e;
        e.type = AutoEventType::kMqtt;
        e.id = topic;
        e.payload = payload.substr(0, 256);
        e.origin = "mqtt";
        ModelLock lock(model_mutex());
        ctx_.automation->post(e);
        return;
    }
    const std::string rest = topic.substr(b.size()); // light/x/set | group/x/set | scene/x/activate
    const std::size_t s1 = rest.find('/');
    const std::size_t s2 = rest.rfind('/');
    if (s1 == std::string::npos || s2 == s1) return reject("bad topic", topic);
    const std::string kind = rest.substr(0, s1);
    const std::string id = rest.substr(s1 + 1, s2 - s1 - 1);
    const std::string verb = rest.substr(s2 + 1);

    ModelLock lock(model_mutex());
    if (kind == "scene" && verb == "activate") {
        if (ctx_.scenes->activate_scene(id, 0, "mqtt") != SceneResult::kOk) return reject("unknown scene", topic);
        AutoEvent e;
        e.type = AutoEventType::kScene;
        e.id = id;
        e.origin = "mqtt";
        ctx_.automation->post(e);
        return;
    }
    cJSON *o = cJSON_ParseWithLength(payload.data(), payload.size());
    if (o == nullptr && (payload == "ON" || payload == "OFF" || payload == "TOGGLE")) {
        o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "state", payload.c_str()); // plain-text convenience
    }
    std::string error;
    if (kind == "light" && verb == "set") {
        const Channel *ch = ctx_.lighting->channel_manager().find(id);
        LightCommandEnvelope env;
        if (ch == nullptr) {
            cJSON_Delete(o);
            return reject("unknown light", topic);
        }
        if (!integration::command_from_ha(o, ch->requested, env.command, error)) {
            cJSON_Delete(o);
            return reject(error.c_str(), topic);
        }
        env.command.channel_id = id;
        env.command.origin = "mqtt";
        ctx_.lighting->enqueue(env);
    } else if (kind == "group" && verb == "set") {
        const Group *g = ctx_.groups->find(id);
        if (g == nullptr) {
            cJSON_Delete(o);
            return reject("unknown group", topic);
        }
        for (const auto &cid : g->channel_ids) {
            const Channel *ch = ctx_.lighting->channel_manager().find(cid);
            if (ch == nullptr) continue;
            LightCommandEnvelope env;
            if (!integration::command_from_ha(o, ch->requested, env.command, error)) break;
            env.command.channel_id = cid;
            env.command.origin = "mqtt";
            ctx_.lighting->enqueue(env);
        }
    } else {
        reject("unsupported topic", topic);
    }
    cJSON_Delete(o);
}

void MqttService::event(const std::string &json)
{
    publish(base() + "/event", json, false);
}

void MqttService::automation_status(const std::string &id, const std::string &json)
{
    publish(base() + "/automation/" + id + "/status", json, true);
}

void MqttService::save_discovered(const std::vector<std::string> &ids)
{
    std::string text;
    if (ctx_.config->load_document(kDoc, text) != ConfigStatus::kOk) return;
    cJSON *o = cJSON_Parse(text.c_str());
    if (o == nullptr) return;
    cJSON_DeleteItemFromObjectCaseSensitive(o, "discovered");
    cJSON_AddItemToObject(o, "discovered", api::string_array(ids));
    ctx_.config->save_document(kDoc, api::to_string(o));
    cJSON_Delete(o);
}

void MqttService::publish_discovery(std::vector<Outbound> &out)
{
    const MqttSettings s = settings();
    if (!s.ha_discovery) {
        disc_next_ = kDiscIdle;
        return;
    }
    const NetworkStatus net = ctx_.network->status();
    integration::DiscoveryContext dc;
    dc.discovery_prefix = s.ha_prefix;
    dc.topic_prefix = s.topic_prefix;
    dc.controller_id = s.controller_id.empty() ? net.hostname : s.controller_id;
    dc.device_name = "Lumen " + dc.controller_id;
    dc.model = ctx_.hardware->profile().name;
    dc.sw_version = kVersion;
    if (!net.sta_ip.empty()) dc.configuration_url = "http://" + net.sta_ip + "/";
    static const std::vector<std::string> effects = {"none", "breathing", "pulse", "rainbow", "color_cycle",
                                                     "candlelight", "sunrise", "sunset", "strobe"};
    const ChannelManager &cm = ctx_.lighting->channel_manager();
    const std::size_t end = std::min(cm.channel_count(), disc_next_ + kDiscChunk);
    for (std::size_t i = disc_next_; i < end; ++i) {
        cJSON *cfg = integration::discovery_config(dc, cm.at(i), effects);
        out.push_back(Outbound{integration::discovery_topic(dc, cm.at(i).id), api::to_string(cfg), true});
        cJSON_Delete(cfg);
    }
    disc_next_ = end;
    if (end < cm.channel_count()) return; // continue next poll (bounded heap)
    disc_next_ = kDiscIdle;
    std::vector<std::string> now;
    for (std::size_t i = 0; i < cm.channel_count(); ++i) now.push_back(cm.at(i).id);
    // Lifecycle: clear configs of lights that no longer exist.
    for (const auto &old : discovered_) {
        if (std::find(now.begin(), now.end(), old) == now.end()) {
            out.push_back(Outbound{integration::discovery_topic(dc, old), "", true});
            out.push_back(Outbound{base() + "/light/" + old + "/state", "", true});
        }
    }
    // Diagnostics sensors.
    const std::string b = base();
    struct Sensor
    {
        const char *key, *name, *unit, *klass;
    };
    const Sensor sensors[] = {{"uptime", "Uptime", "s", "duration"},
                              {"free_heap", "Free memory", "B", "data_size"},
                              {"rssi", "Wi-Fi signal", "dBm", "signal_strength"}};
    for (const auto &se : sensors) {
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "name", se.name);
        const std::string uid = integration::topic_safe(dc.controller_id) + "_" + se.key;
        cJSON_AddStringToObject(o, "unique_id", uid.c_str());
        cJSON_AddStringToObject(o, "state_topic", (b + "/diagnostics").c_str());
        cJSON_AddStringToObject(o, "value_template", (std::string("{{ value_json.") + se.key + " }}").c_str());
        cJSON_AddStringToObject(o, "unit_of_measurement", se.unit);
        cJSON_AddStringToObject(o, "device_class", se.klass);
        cJSON_AddStringToObject(o, "entity_category", "diagnostic");
        cJSON_AddStringToObject(o, "availability_topic", (b + "/status").c_str());
        cJSON *dev = cJSON_AddObjectToObject(o, "device");
        cJSON *ids = cJSON_AddArrayToObject(dev, "identifiers");
        cJSON_AddItemToArray(ids, cJSON_CreateString(("lumen_" + integration::topic_safe(dc.controller_id)).c_str()));
        out.push_back(Outbound{s.ha_prefix + "/sensor/" + integration::topic_safe(dc.controller_id) + "/" + se.key + "/config",
                               api::to_string(o), true});
        cJSON_Delete(o);
    }
    if (now != discovered_) {
        discovered_ = now;
        save_discovered(now);
    }
}

void MqttService::publish_states(bool all, std::vector<Outbound> &out)
{
    std::bitset<64> todo;
    {
        std::lock_guard<std::mutex> l(dirty_mutex_);
        todo = dirty_;
        dirty_.reset();
    }
    const ChannelManager &cm = ctx_.lighting->channel_manager();
    const MqttSettings s = settings();
    const std::string b = base();
    for (std::size_t i = 0; i < cm.channel_count(); ++i) {
        if (!all && !todo.test(i)) continue;
        const Channel &ch = cm.at(i);
        cJSON *st = integration::state_to_ha(ch, ctx_.lighting->active_effect(ch.id));
        out.push_back(Outbound{b + "/light/" + ch.id + "/state", api::to_string(st), s.retain});
        cJSON_Delete(st);
    }
}

void MqttService::poll(uint32_t now_ms)
{
    if (!status().connected) return;
    std::vector<Inbound> msgs;
    {
        std::lock_guard<std::mutex> l(mutex_);
        msgs.swap(inbox_);
    }
    std::vector<Outbound> out;
    {
        ModelLock lock(model_mutex());
        for (const auto &m : msgs) handle_message(m.topic, m.payload);
        if (resubscribe_) {
            resubscribe_ = false;
            void *c = nullptr;
            {
                std::lock_guard<std::mutex> l(mutex_);
                c = client_;
            }
            for (const auto &a : ctx_.automation->automations()) {
                if (c != nullptr && a.enabled && a.trigger.type == TriggerType::kMqttEvent && !a.trigger.target.empty()) {
                    esp_mqtt_client_subscribe(handle(c), a.trigger.target.c_str(), 0);
                }
            }
        }
        if (rediscover_) {
            rediscover_ = false;
            disc_next_ = 0; // (re)start from the first light
        }
        if (disc_next_ != kDiscIdle) publish_discovery(out);
        const bool all = resync_all_;
        resync_all_ = false;
        publish_states(all, out);
        if (now_ms - last_diag_ms_ >= 60000 || last_diag_ms_ == 0) {
            last_diag_ms_ = now_ms;
            cJSON *o = cJSON_CreateObject();
            cJSON_AddNumberToObject(o, "uptime", static_cast<double>(esp_timer_get_time() / 1000000));
            cJSON_AddNumberToObject(o, "free_heap", esp_get_free_heap_size());
            cJSON_AddNumberToObject(o, "rssi", ctx_.network->status().rssi);
            cJSON_AddStringToObject(o, "version", kVersion);
            out.push_back(Outbound{base() + "/diagnostics", api::to_string(o), false});
            cJSON_Delete(o);
        }
    }
    {
        std::lock_guard<std::mutex> l(mutex_);
        for (auto &m : async_) out.push_back(std::move(m));
        async_.clear();
    }
    // Model lock released: a slow broker cannot stall lighting or the API.
    for (const auto &m : out) {
        if (!send_now(m)) {
            resync_all_ = true; // states are re-sent once the link recovers
            break;
        }
    }
}

} // namespace lumen
