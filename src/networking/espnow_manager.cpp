// Lumen — ESP-NOW multi-controller sync. See espnow_manager.hpp.
#include "lumen/networking/espnow_manager.hpp"

#include <algorithm>
#include <cstring>
#include <ctime>

#include "esp_log.h"
#include "esp_now.h"
#include "esp_random.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lumen/core/model_lock.hpp"
#include "lumen/networking/integration_protocol.h"
#include "lumen/version.hpp"
#include "lumen/web/api_util.hpp"
#include "nvs.h"

namespace lumen
{

namespace
{

constexpr const char *TAG = "espnow";
constexpr const char *kDoc = "espnow.json";
constexpr std::size_t kQueueLen = 16;
const uint8_t kBroadcast[6] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
EspNowManager *g_self = nullptr; // for the C receive callback
SemaphoreHandle_t g_sent = nullptr; // given by the send callback (paces bursts)

void send_cb(const esp_now_send_info_t *, esp_now_send_status_t)
{
    if (g_sent != nullptr) xSemaphoreGive(g_sent);
}

uint32_t now_ms() { return static_cast<uint32_t>(esp_timer_get_time() / 1000); }

int64_t unix_now(bool &valid)
{
    const int64_t t = static_cast<int64_t>(std::time(nullptr));
    valid = t > 1700000000;
    return t;
}

std::string random_key()
{
    uint8_t b[16];
    esp_fill_random(b, sizeof(b));
    return integration::hex(b, sizeof(b)); // 32 hex characters
}

const char *type_name(LightType t)
{
    switch (t) {
    case LightType::kSwitch: return "switch";
    case LightType::kDimmer: return "dimmer";
    case LightType::kRgb: return "rgb";
    case LightType::kRgbw: return "rgbw";
    case LightType::kCct: return "cct";
    default: return "other";
    }
}

std::string mac_str(const uint8_t m[6])
{
    char b[18];
    std::snprintf(b, sizeof(b), "%02x:%02x:%02x:%02x:%02x:%02x", m[0], m[1], m[2], m[3], m[4], m[5]);
    return b;
}

} // namespace

// ---------------------------------------------------------------- settings

void EspNowManager::load()
{
    DocConfig doc{kDoc, 1, migrate_additive, nullptr, nullptr};
    ctx_.config->register_document(doc);
    EspNowSettings s;
    std::string text;
    if (ctx_.config->load_document(kDoc, text) == ConfigStatus::kOk) {
        cJSON *o = cJSON_Parse(text.c_str());
        api::get_bool(o, "enabled", s.enabled);
        api::get_string(o, "networkId", s.network_id, 33);
        cJSON_Delete(o);
    }
    std::string key;
    nvs_handle_t h;
    if (nvs_open("espnow", NVS_READONLY, &h) == ESP_OK) {
        char buf[80];
        size_t n = sizeof(buf);
        if (nvs_get_str(h, "key", buf, &n) == ESP_OK) key = buf;
        nvs_close(h);
    }
    uint32_t boots = 0;
    if (nvs_open("sys", NVS_READONLY, &h) == ESP_OK) {
        nvs_get_u32(h, "boots", &boots);
        nvs_close(h);
    }
    std::lock_guard<std::mutex> l(mutex_);
    settings_ = s;
    key_ = key;
    boot_ = boots;
}

EspNowSettings EspNowManager::settings() const
{
    std::lock_guard<std::mutex> l(mutex_);
    return settings_;
}

std::string EspNowManager::key() const
{
    std::lock_guard<std::mutex> l(mutex_);
    return key_;
}

static bool store_key(const std::string &k)
{
    nvs_handle_t h;
    if (nvs_open("espnow", NVS_READWRITE, &h) != ESP_OK) return false;
    const bool ok = nvs_set_str(h, "key", k.c_str()) == ESP_OK && nvs_commit(h) == ESP_OK;
    nvs_close(h);
    return ok;
}

std::string EspNowManager::regenerate_key()
{
    const std::string k = random_key();
    if (!store_key(k)) return key();
    {
        std::lock_guard<std::mutex> l(mutex_);
        key_ = k;
        restart_ = true;
    }
    return k;
}

esp_err_t EspNowManager::update(const EspNowSettings &s, const std::string *key, std::string &error)
{
    if (s.network_id.empty() || s.network_id.size() > 32) {
        error = "Network id must be 1-32 characters";
        return ESP_ERR_INVALID_ARG;
    }
    if (key != nullptr && (key->size() < 16 || key->size() > 64)) {
        error = "The network key must be 16-64 characters (use the same key on every controller)";
        return ESP_ERR_INVALID_ARG;
    }
    cJSON *o = cJSON_CreateObject();
    cJSON_AddBoolToObject(o, "enabled", s.enabled);
    cJSON_AddStringToObject(o, "networkId", s.network_id.c_str());
    const std::string text = api::to_string(o);
    cJSON_Delete(o);
    {
        ModelLock lock(model_mutex());
        if (ctx_.config->save_document(kDoc, text) != ConfigStatus::kOk) {
            error = "Could not save settings";
            return ESP_FAIL;
        }
    }
    std::string k = key != nullptr ? *key : this->key();
    if (k.empty()) k = random_key(); // first enable: generate one
    if (!store_key(k)) {
        error = "Could not store the network key";
        return ESP_FAIL;
    }
    std::lock_guard<std::mutex> l(mutex_);
    settings_ = s;
    key_ = k;
    restart_ = true;
    return ESP_OK;
}

std::string EspNowManager::controller_id() const
{
    return ctx_.network->status().hostname;
}

std::vector<Peer> EspNowManager::peers() const
{
    std::lock_guard<std::mutex> l(mutex_);
    return peers_;
}

EspNowStats EspNowManager::stats() const
{
    std::lock_guard<std::mutex> l(mutex_);
    return stats_;
}

// ---------------------------------------------------------------- radio

void EspNowManager::recv_cb(const esp_now_recv_info_t *info, const uint8_t *data, int len)
{
    // Wi-Fi task: copy only, never block.
    EspNowManager *self = g_self;
    if (self == nullptr || self->queue_ == nullptr || len <= 0 || len > static_cast<int>(espnow::kMaxFrame)) return;
    Rx rx;
    std::memcpy(rx.mac, info->src_addr, 6);
    rx.rssi = info->rx_ctrl != nullptr ? static_cast<int8_t>(info->rx_ctrl->rssi) : 0;
    rx.len = static_cast<uint8_t>(len);
    std::memcpy(rx.data, data, static_cast<std::size_t>(len));
    if (xQueueSend(static_cast<QueueHandle_t>(self->queue_), &rx, 0) != pdTRUE) {
        std::lock_guard<std::mutex> l(self->mutex_);
        self->stats_.rx_queue_full++;
    }
}

esp_err_t EspNowManager::init_radio()
{
    esp_err_t err = esp_now_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_now_init: %s", esp_err_to_name(err));
        return err;
    }
    err = esp_now_register_recv_cb(&EspNowManager::recv_cb);
    if (err == ESP_OK) err = esp_now_register_send_cb(&send_cb);
    if (err == ESP_OK) {
        esp_now_peer_info_t peer{};
        std::memcpy(peer.peer_addr, kBroadcast, 6);
        peer.channel = 0; // current Wi-Fi channel (same AP on every controller)
        peer.ifidx = WIFI_IF_STA;
        peer.encrypt = false; // authenticated by the frame HMAC
        err = esp_now_add_peer(&peer);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ESP-NOW setup failed: %s", esp_err_to_name(err));
        esp_now_deinit();
        return err;
    }
    uint8_t ch = 0;
    wifi_second_chan_t second;
    esp_wifi_get_channel(&ch, &second);
    // Modem sleep makes the radio miss broadcasts; controllers are mains powered.
    esp_wifi_set_ps(WIFI_PS_NONE);
    ESP_LOGI(TAG, "ESP-NOW on channel %u, network '%s'", ch, settings().network_id.c_str());
    running_ = true;
    return ESP_OK;
}

void EspNowManager::deinit_radio()
{
    if (!running_) return;
    running_ = false;
    esp_now_unregister_recv_cb();
    esp_now_unregister_send_cb();
    esp_now_deinit();
    esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
}

esp_err_t EspNowManager::start()
{
    if (started_) return ESP_OK;
    started_ = true;
    g_self = this;
    queue_ = xQueueCreate(kQueueLen, sizeof(Rx));
    g_sent = xSemaphoreCreateBinary();
    if (queue_ == nullptr) return ESP_ERR_NO_MEM;
    {
        std::lock_guard<std::mutex> l(mutex_);
        restart_ = true;
    }
    if (xTaskCreatePinnedToCore(&EspNowManager::task_entry, "espnow", 6144, this, 3, nullptr, 0) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void EspNowManager::task_entry(void *arg)
{
    static_cast<EspNowManager *>(arg)->run();
}

bool EspNowManager::send(espnow::Type type, const std::string &payload)
{
    if (!running_) return false;
    const EspNowSettings s = settings();
    const std::string k = key();
    bool valid = false;
    const int64_t t = unix_now(valid);
    espnow::Frame f;
    f.type = type;
    f.flags = valid ? espnow::kFlagTsValid : 0;
    f.network = espnow::network_tag(s.network_id);
    f.msg_id = esp_random() | 1u;
    f.timestamp = valid ? static_cast<uint32_t>(t) : 0;
    f.payload = payload;
    std::vector<uint8_t> bytes;
    esp_err_t err;
    {
        std::lock_guard<std::mutex> l(send_mutex_);
        // Boot count in the high bits keeps seq increasing across reboots.
        f.seq = (boot_ << 20) | (++seq_counter_ & 0xFFFFF);
        if (!espnow::encode(f, k, bytes)) return false;
        xSemaphoreTake(g_sent, 0); // clear a stale completion
        err = esp_now_send(kBroadcast, bytes.data(), bytes.size());
        // Broadcasts are not acknowledged: frames queued faster than the
        // radio sends them are dropped silently, so wait for completion.
        if (err == ESP_OK) xSemaphoreTake(g_sent, pdMS_TO_TICKS(30));
    }
    std::lock_guard<std::mutex> l(mutex_);
    if (err == ESP_OK) stats_.tx++;
    else stats_.tx_failed++;
    return err == ESP_OK;
}

// ---------------------------------------------------------------- main loop

#ifdef LUMEN_TEST_HOOKS
namespace
{
std::atomic<uint32_t> g_rx_hold_ms{0};
void test_hold()
{
    const uint32_t ms = g_rx_hold_ms.load();
    if (ms > 0) vTaskDelay(pdMS_TO_TICKS(ms));
}
} // namespace

void EspNowManager::test_set_rx_hold_ms(uint32_t ms)
{
    g_rx_hold_ms = ms > 3000 ? 3000 : ms; // below the 5 s task watchdog
}
#else
namespace
{
inline void test_hold() {}
} // namespace
#endif

void EspNowManager::run()
{
    uint32_t last_hello = 0, last_states = 0, last_resync = 0;
    Rx rx;
    const bool wdt = esp_task_wdt_add(nullptr) == ESP_OK; // loop wakes at least every 50 ms
    if (!wdt) ESP_LOGW(TAG, "espnow task not on the task watchdog");
    for (;;) {
        if (wdt) esp_task_wdt_reset();
        bool restart = false;
        {
            std::lock_guard<std::mutex> l(mutex_);
            restart = restart_;
            restart_ = false;
        }
        if (restart) {
            deinit_radio();
            {
                std::lock_guard<std::mutex> l(mutex_);
                peers_.clear();
            }
            if (settings().enabled && !key().empty()) {
                if (init_radio() == ESP_OK) {
                    last_hello = 0;
                    resync_ = true;
                } else {
                    // Wi-Fi not up yet (boot) or busy: try again shortly.
                    vTaskDelay(pdMS_TO_TICKS(2000));
                    std::lock_guard<std::mutex> l(mutex_);
                    restart_ = true;
                }
            }
        }
        if (xQueueReceive(static_cast<QueueHandle_t>(queue_), &rx, pdMS_TO_TICKS(50)) == pdTRUE) {
            test_hold();
            if (running_) handle(rx);
            // Drain what is already queued before housekeeping.
            while (running_ && xQueueReceive(static_cast<QueueHandle_t>(queue_), &rx, 0) == pdTRUE) {
                test_hold();
                handle(rx);
            }
        }
        if (!running_) {
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }
        const uint32_t now = now_ms();
        if (last_hello == 0 || now - last_hello >= kHelloMs) {
            last_hello = now;
            // The network manager re-enables modem sleep whenever it
            // restarts Wi-Fi; a dozing radio hears broadcasts only at beacon
            // intervals (~300 ms). Keep it awake while ESP-NOW runs.
            wifi_ps_type_t ps = WIFI_PS_NONE;
            if (esp_wifi_get_ps(&ps) == ESP_OK && ps != WIFI_PS_NONE) esp_wifi_set_ps(WIFI_PS_NONE);
            send_hello();
            check_offline(now);
        }
        // State changes go out at most every 50 ms (coalesced per light).
        // Full state every 30 s heals frames lost on the air.
        if (now - last_resync >= 30000) {
            last_resync = now;
            resync_ = true;
        }
        if (now - last_states >= 50) {
            last_states = now;
            const bool all = resync_ || announce_;
            resync_ = announce_ = false;
            send_states(all);
        }
    }
}

void EspNowManager::send_hello()
{
    const NetworkStatus net = ctx_.network->status();
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "id", net.hostname.c_str());
    cJSON_AddStringToObject(o, "ip", net.sta_ip.c_str());
    cJSON_AddStringToObject(o, "v", kVersion);
    {
        ModelLock lock(model_mutex());
        cJSON_AddNumberToObject(o, "lc", static_cast<double>(ctx_.lighting->channel_manager().channel_count()));
    }
    send(espnow::Type::kHello, api::to_string(o));
    cJSON_Delete(o);
}

std::string EspNowManager::state_payload(std::size_t i)
{
    const Channel &ch = ctx_.lighting->channel_manager().at(i);
    const LightState &s = ch.applied; // actual output after hardware application
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "l", ch.id.c_str());
    cJSON_AddStringToObject(o, "n", ch.name.substr(0, 32).c_str());
    cJSON_AddStringToObject(o, "t", type_name(ch.type));
    cJSON_AddNumberToObject(o, "p", s.power ? 1 : 0);
    cJSON_AddNumberToObject(o, "b", s.brightness);
    if (ch.type == LightType::kRgb || ch.type == LightType::kRgbw) {
        cJSON *c = cJSON_AddArrayToObject(o, "c");
        for (int v : {static_cast<int>(s.red), static_cast<int>(s.green), static_cast<int>(s.blue), static_cast<int>(s.white)}) {
            cJSON_AddItemToArray(c, cJSON_CreateNumber(v));
        }
    }
    if (ch.type == LightType::kCct) cJSON_AddNumberToObject(o, "k", s.color_temperature);
    const std::string out = api::to_string(o);
    cJSON_Delete(o);
    return out;
}

void EspNowManager::send_states(bool all)
{
    std::bitset<64> todo;
    {
        std::lock_guard<std::mutex> l(dirty_mutex_);
        todo = dirty_;
        dirty_.reset();
    }
    std::vector<std::string> payloads;
    {
        ModelLock lock(model_mutex());
        const ChannelManager &cm = ctx_.lighting->channel_manager();
        for (std::size_t i = 0; i < cm.channel_count() && i < 64; ++i) {
            if (all || todo.test(i)) payloads.push_back(state_payload(i));
        }
    }
    for (const auto &p : payloads) send(espnow::Type::kState, p);
}

void EspNowManager::light_changed(std::size_t index, const std::string &origin)
{
    if (index >= 64) return;
    {
        std::lock_guard<std::mutex> l(dirty_mutex_);
        dirty_.set(index);
    }
    if (origin.rfind("espnow:", 0) != 0) {
        bool valid = false;
        const int64_t t = unix_now(valid);
        if (valid) last_local_unix_[index] = t;
    }
}

// ---------------------------------------------------------------- receive

Peer *EspNowManager::peer_for(const uint8_t mac[6], bool create)
{
    for (auto &p : peers_) {
        if (std::memcmp(p.mac.data(), mac, 6) == 0) return &p;
    }
    if (!create) return nullptr;
    if (peers_.size() >= kMaxPeers) {
        stats_.peers_full++;
        return nullptr;
    }
    Peer p;
    std::memcpy(p.mac.data(), mac, 6);
    peers_.push_back(p);
    return &peers_.back();
}

void EspNowManager::handle(const Rx &rx)
{
    const EspNowSettings s = settings();
    espnow::Frame f;
    const espnow::Reject r = espnow::decode(rx.data, rx.len, key(), espnow::network_tag(s.network_id), f);
    {
        std::lock_guard<std::mutex> l(mutex_);
        stats_.rx++;
        switch (r) {
        case espnow::Reject::kNone: break;
        case espnow::Reject::kMalformed: stats_.malformed++; return;
        case espnow::Reject::kVersion: stats_.version++; return;
        case espnow::Reject::kNetwork: stats_.network++; return;
        case espnow::Reject::kAuth: stats_.auth++; return;
        }
    }
    std::array<uint8_t, 6> mac;
    std::memcpy(mac.data(), rx.mac, 6);
    bool valid = false;
    const int64_t t = unix_now(valid);
    const auto verdict = replay_.check(mac, f, t, valid);
    if (verdict != espnow::ReplayFilter::Verdict::kAccept) {
        std::lock_guard<std::mutex> l(mutex_);
        if (verdict == espnow::ReplayFilter::Verdict::kReplay) stats_.replay++;
        else if (verdict == espnow::ReplayFilter::Verdict::kDuplicate) stats_.duplicate++;
        else if (verdict == espnow::ReplayFilter::Verdict::kStale) stats_.stale++;
        else stats_.peers_full++;
        return;
    }
    cJSON *p = cJSON_ParseWithLength(f.payload.data(), f.payload.size());
    if (!cJSON_IsObject(p)) {
        cJSON_Delete(p);
        std::lock_guard<std::mutex> l(mutex_);
        stats_.malformed++;
        return;
    }
    switch (f.type) {
    case espnow::Type::kHello: on_hello(rx, p); break;
    case espnow::Type::kState: on_state(rx, p); break;
    case espnow::Type::kCommand: on_command(rx, f, p); break;
    case espnow::Type::kScene: on_scene(p); break;
    }
    cJSON_Delete(p);
}

void EspNowManager::on_hello(const Rx &rx, const cJSON *p)
{
    const std::string id = api::str_or(p, "id", "");
    if (id.empty() || id.size() > 32) return;
    bool appeared = false;
    {
        std::lock_guard<std::mutex> l(mutex_);
        Peer *peer = peer_for(rx.mac, true);
        if (peer == nullptr) return;
        appeared = !peer->online;
        peer->id = id;
        peer->name = id;
        peer->ip = api::str_or(p, "ip", "").substr(0, 15);
        peer->version = api::str_or(p, "v", "").substr(0, 31);
        peer->rssi = rx.rssi;
        peer->last_seen_ms = now_ms();
        peer->online = true;
    }
    if (appeared) {
        ESP_LOGI(TAG, "controller %s (%s) online", id.c_str(), mac_str(rx.mac).c_str());
        announce_ = true; // let the newcomer learn our states
        ctx_.ws->broadcast("controller_discovered", "{\"id\":\"" + id + "\"}", false);
    }
}

void EspNowManager::on_state(const Rx &rx, const cJSON *p)
{
    RemoteLight rl;
    rl.id = api::str_or(p, "l", "");
    if (rl.id.empty() || rl.id.size() > 48) return;
    rl.name = api::str_or(p, "n", rl.id).substr(0, 32);
    rl.type = api::str_or(p, "t", "dimmer");
    double v = 0;
    if (api::get_number(p, "p", v)) rl.state.power = v != 0;
    if (api::get_number(p, "b", v) && v >= 0 && v <= 100) rl.state.brightness = static_cast<uint8_t>(v);
    const cJSON *c = cJSON_GetObjectItemCaseSensitive(p, "c");
    if (cJSON_IsArray(c) && cJSON_GetArraySize(c) == 4) {
        uint8_t *dst[4] = {&rl.state.red, &rl.state.green, &rl.state.blue, &rl.state.white};
        for (int i = 0; i < 4; ++i) {
            const cJSON *n = cJSON_GetArrayItem(c, i);
            if (cJSON_IsNumber(n) && n->valuedouble >= 0 && n->valuedouble <= 255) *dst[i] = static_cast<uint8_t>(n->valuedouble);
        }
    }
    if (api::get_number(p, "k", v) && v >= 0 && v <= 20000) rl.state.color_temperature = static_cast<uint16_t>(v);
    std::string peer_id;
    {
        std::lock_guard<std::mutex> l(mutex_);
        // States may arrive before the sender's first HELLO (it answers our
        // HELLO immediately): keep them; the HELLO fills in the identity.
        Peer *peer = peer_for(rx.mac, true);
        if (peer == nullptr) return;
        peer->last_seen_ms = now_ms();
        peer->rssi = rx.rssi;
        auto it = std::find_if(peer->lights.begin(), peer->lights.end(), [&](const RemoteLight &x) { return x.id == rl.id; });
        if (it != peer->lights.end()) *it = rl;
        else if (peer->lights.size() < kMaxRemoteLights) peer->lights.push_back(rl);
        peer_id = peer->id;
    }
    if (peer_id.empty()) return; // UI learns about it once the peer is identified
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "controllerId", peer_id.c_str());
    cJSON_AddStringToObject(o, "lightId", rl.id.c_str());
    cJSON_AddBoolToObject(o, "power", rl.state.power);
    cJSON_AddNumberToObject(o, "brightness", rl.state.brightness);
    ctx_.ws->broadcast("peer_light_state", api::to_string(o));
    cJSON_Delete(o);
}

void EspNowManager::on_command(const Rx &rx, const espnow::Frame &f, const cJSON *p)
{
    if (api::str_or(p, "to", "") != controller_id()) return; // addressed to another controller
    const std::string light = api::str_or(p, "l", "");
    std::string origin = "espnow:";
    {
        std::lock_guard<std::mutex> l(mutex_);
        Peer *peer = peer_for(rx.mac, false);
        origin += peer != nullptr && !peer->id.empty() ? peer->id : mac_str(rx.mac);
    }
    ModelLock lock(model_mutex());
    const ChannelManager &cm = ctx_.lighting->channel_manager();
    const std::size_t index = cm.index_of(light);
    std::string error;
    bool ok = index != ChannelManager::kNotFound;
    // Local input wins over an older synchronised command.
    if (ok && (f.flags & espnow::kFlagTsValid) && index < 64 && last_local_unix_[index] != 0 &&
        static_cast<int64_t>(f.timestamp) < last_local_unix_[index]) {
        ok = false;
        error = "stale";
    }
    LightCommandEnvelope env;
    if (ok) ok = integration::command_from_ha(p, cm.at(index).requested, env.command, error);
    if (ok) {
        env.command.channel_id = light;
        env.command.origin = origin;
        ok = ctx_.lighting->enqueue(env) == EnqueueResult::kOk;
    }
    std::lock_guard<std::mutex> l(mutex_);
    if (ok) stats_.commands_applied++;
    else stats_.commands_rejected++;
    if (!ok) ESP_LOGW(TAG, "command for '%s' from %s rejected: %s", light.substr(0, 32).c_str(), origin.c_str(), error.empty() ? "unknown light" : error.c_str());
}

void EspNowManager::on_scene(const cJSON *p)
{
    const std::string id = api::str_or(p, "s", "");
    ModelLock lock(model_mutex());
    if (ctx_.scenes->find(id) == nullptr) return; // not every controller has every scene
    if (ctx_.scenes->activate_scene(id, 0, "espnow:scene") == SceneResult::kOk) {
        std::lock_guard<std::mutex> l(mutex_);
        stats_.scenes_applied++;
    }
}

void EspNowManager::check_offline(uint32_t now)
{
    std::vector<std::string> gone;
    {
        std::lock_guard<std::mutex> l(mutex_);
        for (auto &p : peers_) {
            if (p.online && now - p.last_seen_ms > kOfflineMs) {
                p.online = false;
                gone.push_back(p.id);
            }
        }
    }
    for (const auto &id : gone) {
        ESP_LOGW(TAG, "controller %s offline", id.c_str());
        ctx_.ws->broadcast("controller_disconnected", "{\"id\":\"" + id + "\"}", false);
    }
}

// ---------------------------------------------------------------- outbound API

bool EspNowManager::send_command(const std::string &peer_id, const std::string &light_id, const cJSON *cmd, std::string &error)
{
    if (!running_) {
        error = "ESP-NOW is disabled";
        return false;
    }
    {
        std::lock_guard<std::mutex> l(mutex_);
        const auto it = std::find_if(peers_.begin(), peers_.end(), [&](const Peer &p) { return p.id == peer_id; });
        if (it == peers_.end() || !it->online) {
            error = "controller '" + peer_id + "' is not online";
            return false;
        }
    }
    cJSON *o = cJSON_Duplicate(cmd, 1);
    cJSON_AddStringToObject(o, "to", peer_id.c_str());
    cJSON_AddStringToObject(o, "l", light_id.c_str());
    const std::string payload = api::to_string(o);
    cJSON_Delete(o);
    if (payload.size() > espnow::kMaxPayload) {
        error = "command too large";
        return false;
    }
    if (!send(espnow::Type::kCommand, payload)) {
        error = "radio send failed";
        return false;
    }
    std::lock_guard<std::mutex> l(mutex_);
    stats_.commands_sent++;
    return true;
}

bool EspNowManager::send_scene(const std::string &scene_id)
{
    return send(espnow::Type::kScene, "{\"s\":\"" + scene_id.substr(0, 48) + "\"}");
}

} // namespace lumen
