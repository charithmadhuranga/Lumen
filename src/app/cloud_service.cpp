// Lumen — controller cloud link. See cloud_service.hpp.

#include "lumen/app/cloud_service.hpp"

#include <cstring>
#include <ctime>

#include "core/cJSON.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"
#include "lumen/core/config_manager.hpp"
#include "lumen/hardware/board_service.h"
#include "lumen/networking/network_manager.hpp"
#include "lumen/version.hpp"
#include "nvs.h"

namespace lumen
{
namespace
{

constexpr const char *TAG = "cloud";
constexpr const char *kNvs = "cloud";
constexpr uint32_t kPersistEveryMs = 10 * 60 * 1000;
constexpr std::size_t kMaxReply = 1024;
constexpr int kHttpTimeoutMs = 3000; // < the 5 s task watchdog of the housekeeping task

uint32_t uptime_s() { return static_cast<uint32_t>(esp_timer_get_time() / 1000000); }

bool wall_synced(int64_t now) { return now > 1700000000; }

const char *state_name(CloudService::State s)
{
    switch (s) {
    case CloudService::State::kOff: return "off";
    case CloudService::State::kPairing: return "pairing";
    case CloudService::State::kPaired: return "paired";
    case CloudService::State::kPairFailed: return "pair_failed";
    }
    return "off";
}

} // namespace

CloudService::CloudService(ConfigManager &config, NetworkManager &network, BoardService &boards)
    : config_(config), network_(network), boards_(boards)
{
}

esp_err_t CloudService::init()
{
    DocConfig doc{cloud::kDocument, cloud::kSchemaVersion, migrate_additive, nullptr, nullptr};
    if (!config_.register_document(doc)) ESP_LOGE(TAG, "config registry full: cloud.json not registered");
    std::lock_guard<std::mutex> lock(mutex_);
    nvs_handle_t h;
    if (nvs_open(kNvs, NVS_READONLY, &h) == ESP_OK) {
        std::size_t len = key_.size();
        have_key_ = nvs_get_blob(h, "key", key_.data(), &len) == ESP_OK && len == key_.size();
        int64_t t = 0;
        if (nvs_get_i64(h, "last_time", &t) == ESP_OK && t > 0) last_time_ = last_time_saved_ = t;
        nvs_close(h);
    }
    std::string text;
    if (config_.load_document(cloud::kDocument, text) == ConfigStatus::kOk) {
        std::string err;
        cloud::Settings s;
        if (!cloud::parse_settings(text, s, err)) {
            ESP_LOGW(TAG, "cloud.json ignored: %s", err.c_str());
        } else if (s.paired && !have_key_) {
            last_error_ = "paired, but the local key is missing; pair again";
            state_ = State::kPairFailed;
            ESP_LOGW(TAG, "%s", last_error_.c_str());
        } else {
            settings_ = s;
            state_ = s.paired ? State::kPaired : State::kOff;
        }
    }
    ESP_LOGI(TAG, "cloud link: %s", state_name(state_));
    return ESP_OK;
}

int64_t CloudService::ticket_now()
{
    const int64_t wall = static_cast<int64_t>(std::time(nullptr));
    const bool synced = wall_synced(wall);
    if (synced && wall > last_time_) last_time_ = wall;
    return cloud::ticket_clock(wall, synced, last_time_);
}

CloudService::Verdict CloudService::verify(const std::string &t)
{
    std::lock_guard<std::mutex> lock(mutex_);
    Verdict v;
    if (state_ != State::kPaired || !have_key_) {
        v.reason = ticket::Reason::kDevice; // not paired: no ticket can be for us
        ++rejected_;
        ++rejected_by_reason_[static_cast<int>(v.reason)];
        return v;
    }
    const ticket::Result r = ticket::verify(t, key_.data(), settings_.device_id, settings_.ticket_epoch, ticket_now());
    v.reason = r.reason;
    if (!r.ok()) {
        ++rejected_;
        ++rejected_by_reason_[static_cast<int>(r.reason)];
        return v;
    }
    v.ok = true;
    v.admin = r.claims.scope == ticket::Scope::kAdmin;
    v.gateway = r.claims.sub == "g:" + settings_.gateway_id;
    ++accepted_;
    if (v.gateway) last_gateway_ticket_s_ = uptime_s();
    return v;
}

bool CloudService::request_pair(const std::string &claim, const std::string &url, std::string &error)
{
    if (!cloud::is_claim_token(claim)) {
        error = "claimToken must be a claim code from the Lumen app (lct_...)";
        return false;
    }
    if (!cloud::is_lan_gateway_url(url)) {
        error = "gatewayUrl must be http://<local network IPv4>[:port] of your Lumen Gateway";
        return false;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    if (job_ || state_ == State::kPairing) {
        error = "pairing is already in progress";
        return false;
    }
    job_ = true;
    job_claim_ = claim;
    job_url_ = url;
    state_ = State::kPairing;
    last_error_.clear();
    return true;
}

void CloudService::poll(uint32_t uptime_ms)
{
    std::string claim, url;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (job_) {
            job_ = false;
            claim.swap(job_claim_);
            url.swap(job_url_);
        }
    }
    if (!claim.empty()) run_pairing(claim, url);
    if (uptime_ms - last_persist_ms_ >= kPersistEveryMs) {
        last_persist_ms_ = uptime_ms;
        persist_last_time(false);
    }
}

void CloudService::run_pairing(const std::string &claim, const std::string &url)
{
    const NetworkStatus net = network_.status();
    const std::string ip = !net.sta_ip.empty() ? net.sta_ip : net.eth_ip;
    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    char mac_text[18];
    std::snprintf(mac_text, sizeof(mac_text), "%02x:%02x:%02x:%02x:%02x:%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    cloud::Hardware hw{boards_.active_id(), boards_.active_id(), mac_text, CONFIG_IDF_TARGET};
    const std::string body = cloud::register_request(claim, hw, kVersion, net.hostname, "http://" + ip);

    std::string reply, error;
    int status = 0;
    if (ip.empty()) {
        error = "no network address yet";
    } else {
        esp_http_client_config_t cfg = {};
        const std::string endpoint = url + "/local/v1/register";
        cfg.url = endpoint.c_str();
        cfg.method = HTTP_METHOD_POST;
        cfg.timeout_ms = kHttpTimeoutMs;
        cfg.disable_auto_redirect = true;
        cfg.buffer_size = 1024;
        esp_http_client_handle_t c = esp_http_client_init(&cfg);
        esp_task_wdt_reset();
        if (c == nullptr) {
            error = "out of memory";
        } else {
            esp_http_client_set_header(c, "Content-Type", "application/json");
            esp_err_t err = esp_http_client_open(c, static_cast<int>(body.size()));
            if (err == ESP_OK && esp_http_client_write(c, body.data(), static_cast<int>(body.size())) != static_cast<int>(body.size())) {
                err = ESP_FAIL;
            }
            if (err == ESP_OK && esp_http_client_fetch_headers(c) < 0) err = ESP_FAIL;
            if (err == ESP_OK) {
                status = esp_http_client_get_status_code(c);
                char buf[256];
                int n;
                while ((n = esp_http_client_read(c, buf, sizeof(buf))) > 0) {
                    reply.append(buf, static_cast<std::size_t>(n));
                    if (reply.size() > kMaxReply) {
                        err = ESP_ERR_INVALID_SIZE;
                        break;
                    }
                }
            }
            if (err != ESP_OK) error = std::string("could not reach the gateway (") + esp_err_to_name(err) + ")";
            esp_http_client_cleanup(c);
        }
        esp_task_wdt_reset();
    }

    cloud::Registration reg;
    if (error.empty() && !cloud::parse_register_response(reply, reg, error) && error.empty()) {
        error = "unexpected reply from the gateway (HTTP " + std::to_string(status) + ")";
    }
    if (!error.empty()) {
        std::lock_guard<std::mutex> lock(mutex_);
        state_ = settings_.paired && have_key_ ? State::kPaired : State::kPairFailed;
        last_error_ = error;
        ESP_LOGW(TAG, "pairing failed: %s", error.c_str());
        return;
    }

    // Secrets first (NVS), then the document: a crash in between leaves a key
    // without a pairing, which init() ignores, never a pairing without a key.
    nvs_handle_t h;
    esp_err_t err = nvs_open(kNvs, NVS_READWRITE, &h);
    if (err == ESP_OK) {
        err = nvs_set_blob(h, "key", reg.local_key.data(), reg.local_key.size());
        if (err == ESP_OK) err = nvs_commit(h);
        nvs_close(h);
    }
    cloud::Settings s;
    s.paired = true;
    s.device_id = reg.device_id;
    s.gateway_id = reg.gateway_id;
    s.gateway_url = url;
    s.ticket_epoch = reg.ticket_epoch;
    const int64_t wall = static_cast<int64_t>(std::time(nullptr));
    s.paired_at = wall_synced(wall) ? wall : 0;
    if (err != ESP_OK || !save_settings(s)) {
        std::lock_guard<std::mutex> lock(mutex_);
        state_ = State::kPairFailed;
        last_error_ = "could not store the pairing";
        ESP_LOGE(TAG, "pairing storage failed (%s)", esp_err_to_name(err));
        return;
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        settings_ = s;
        key_ = reg.local_key;
        have_key_ = true;
        state_ = State::kPaired;
        last_error_.clear();
    }
    std::memset(reg.local_key.data(), 0, reg.local_key.size());
    ESP_LOGI(TAG, "paired with gateway %s (device %s)", s.gateway_id.c_str(), s.device_id.c_str());
    notify();
}

bool CloudService::save_settings(const cloud::Settings &s)
{
    return config_.save_document(cloud::kDocument, cloud::to_json(s)) == ConfigStatus::kOk;
}

esp_err_t CloudService::unpair()
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(kNvs, NVS_READWRITE, &h);
    if (err == ESP_OK) {
        nvs_erase_key(h, "key"); // ESP_ERR_NVS_NOT_FOUND is fine
        err = nvs_commit(h);
        nvs_close(h);
    }
    if (err != ESP_OK) return err;
    if (!save_settings(cloud::Settings{})) return ESP_FAIL;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        settings_ = cloud::Settings{};
        key_.fill(0);
        have_key_ = false;
        state_ = State::kOff;
        last_error_.clear();
    }
    ESP_LOGI(TAG, "unpaired");
    notify();
    return ESP_OK;
}

bool CloudService::raise_epoch(uint32_t epoch, std::string &error)
{
    cloud::Settings s;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (state_ != State::kPaired) {
            error = "not paired";
            return false;
        }
        if (epoch <= settings_.ticket_epoch) {
            error = "the ticket epoch can only increase";
            return false;
        }
        s = settings_;
    }
    s.ticket_epoch = epoch;
    if (!save_settings(s)) {
        error = "could not store the new epoch";
        return false;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    settings_.ticket_epoch = epoch;
    return true;
}

void CloudService::persist_last_time(bool force)
{
    int64_t t;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        ticket_now();
        t = last_time_;
        if (t <= last_time_saved_ || (!force && t - last_time_saved_ < 60)) return;
    }
    nvs_handle_t h;
    if (nvs_open(kNvs, NVS_READWRITE, &h) != ESP_OK) return;
    if (nvs_set_i64(h, "last_time", t) == ESP_OK && nvs_commit(h) == ESP_OK) {
        std::lock_guard<std::mutex> lock(mutex_);
        last_time_saved_ = t;
    }
    nvs_close(h);
}

void CloudService::on_shutdown() { persist_last_time(true); }

bool CloudService::paired()
{
    std::lock_guard<std::mutex> lock(mutex_);
    return state_ == State::kPaired;
}

std::string CloudService::device_id()
{
    std::lock_guard<std::mutex> lock(mutex_);
    return state_ == State::kPaired ? settings_.device_id : std::string();
}

void CloudService::notify()
{
    if (on_change_) on_change_();
}

cJSON *CloudService::status_json()
{
    std::lock_guard<std::mutex> lock(mutex_);
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "mode", settings_.paired && state_ == State::kPaired ? "gateway" : "off");
    cJSON_AddStringToObject(o, "state", state_name(state_));
    if (!last_error_.empty()) {
        cJSON_AddStringToObject(o, "lastError", last_error_.c_str());
    } else {
        cJSON_AddNullToObject(o, "lastError");
    }
    if (state_ == State::kPaired) {
        cJSON_AddStringToObject(o, "deviceId", settings_.device_id.c_str());
        cJSON_AddStringToObject(o, "gatewayId", settings_.gateway_id.c_str());
        cJSON_AddStringToObject(o, "gatewayUrl", settings_.gateway_url.c_str());
        cJSON_AddNumberToObject(o, "ticketEpoch", settings_.ticket_epoch);
        cJSON_AddNumberToObject(o, "pairedAt", static_cast<double>(settings_.paired_at));
    }
    if (last_gateway_ticket_s_ != 0) {
        cJSON_AddNumberToObject(o, "gatewaySeenSecAgo", uptime_s() - last_gateway_ticket_s_);
    } else {
        cJSON_AddNullToObject(o, "gatewaySeenSecAgo");
    }
    cJSON *t = cJSON_AddObjectToObject(o, "tickets");
    cJSON_AddNumberToObject(t, "accepted", accepted_);
    cJSON_AddNumberToObject(t, "rejected", rejected_);
    cJSON *by = cJSON_AddObjectToObject(t, "rejectedBy");
    for (int i = 1; i < 9; ++i) {
        if (rejected_by_reason_[i] != 0) {
            cJSON_AddNumberToObject(by, ticket::reason_name(static_cast<ticket::Reason>(i)), rejected_by_reason_[i]);
        }
    }
    return o;
}

} // namespace lumen
