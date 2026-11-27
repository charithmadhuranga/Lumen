// Lumen — TCP/UDP control servers. See control_servers.hpp
#include "lumen/networking/control_servers.hpp"
#include "lumen/app/cloud_service.hpp"

#include <cstring>
#include <ctime>

#include "esp_log.h"
#include "esp_task_wdt.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lumen/core/model_lock.hpp"
#include "lumen/version.hpp"
#include "lumen/web/api_util.hpp"
#include "lwip/sockets.h"
#include "nvs.h"

namespace lumen
{

namespace
{
const char *TAG = "ctrl";
constexpr const char *kDoc = "integrations.json";
constexpr uint32_t kAuthTimeoutMs = 10000;
constexpr uint32_t kIdleTimeoutMs = 300000;
constexpr uint16_t kUdpRatePerSecond = 20;

uint32_t now_ms()
{
    return static_cast<uint32_t>(esp_timer_get_time() / 1000);
}

bool ct_equal(const std::string &a, const std::string &b)
{
    if (a.size() != b.size() || a.empty()) return false;
    uint8_t d = 0;
    for (std::size_t i = 0; i < a.size(); ++i) d |= static_cast<uint8_t>(a[i] ^ b[i]);
    return d == 0;
}
} // namespace

void ControlServers::load()
{
    DocConfig doc{kDoc, 1, migrate_additive, nullptr, nullptr};
    ctx_.config->register_document(doc);
    ControlSettings s;
    std::string text;
    if (ctx_.config->load_document(kDoc, text) == ConfigStatus::kOk) {
        cJSON *o = cJSON_Parse(text.c_str());
        if (o != nullptr) {
            double v = 0;
            api::get_bool(o, "tcpEnabled", s.tcp_enabled);
            if (api::get_number(o, "tcpPort", v)) s.tcp_port = static_cast<uint16_t>(v);
            api::get_bool(o, "udpEnabled", s.udp_enabled);
            api::get_bool(o, "udpControl", s.udp_control);
            api::get_bool(o, "udpAnnounce", s.udp_announce);
            if (api::get_number(o, "udpPort", v)) s.udp_port = static_cast<uint16_t>(v);
            cJSON_Delete(o);
        }
    }
    // Integration token (NVS). Generated once; shown only to signed-in users.
    std::string tok;
    nvs_handle_t h;
    if (nvs_open("intg", NVS_READWRITE, &h) == ESP_OK) {
        std::size_t len = 0;
        if (nvs_get_str(h, "token", nullptr, &len) == ESP_OK && len == 33) {
            tok.resize(len);
            nvs_get_str(h, "token", &tok[0], &len);
            tok.resize(32);
        } else {
            uint8_t raw[16];
            esp_fill_random(raw, sizeof(raw));
            tok = integration::hex(raw, sizeof(raw));
            nvs_set_str(h, "token", tok.c_str());
            nvs_commit(h);
        }
        nvs_close(h);
    }
    std::lock_guard<std::mutex> l(mutex_);
    settings_ = s;
    token_ = tok;
}

ControlSettings ControlServers::settings() const
{
    std::lock_guard<std::mutex> l(mutex_);
    return settings_;
}

ControlStatus ControlServers::status() const
{
    std::lock_guard<std::mutex> l(mutex_);
    return status_;
}

std::string ControlServers::token() const
{
    std::lock_guard<std::mutex> l(mutex_);
    return token_;
}

std::string ControlServers::regenerate_token()
{
    uint8_t raw[16];
    esp_fill_random(raw, sizeof(raw));
    const std::string tok = integration::hex(raw, sizeof(raw));
    nvs_handle_t h;
    if (nvs_open("intg", NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_str(h, "token", tok.c_str());
        nvs_commit(h);
        nvs_close(h);
    }
    std::lock_guard<std::mutex> l(mutex_);
    token_ = tok;
    restart_ = true; // drop sessions authenticated with the old token
    return tok;
}

esp_err_t ControlServers::update(const ControlSettings &s, std::string &error)
{
    if (s.tcp_port < 1024 || s.udp_port < 1024 || s.tcp_port == 80 || s.tcp_port == s.udp_port) {
        error = "Ports must be 1024-65535, distinct, and not the web port";
        return ESP_ERR_INVALID_ARG;
    }
    cJSON *o = cJSON_CreateObject();
    cJSON_AddBoolToObject(o, "tcpEnabled", s.tcp_enabled);
    cJSON_AddNumberToObject(o, "tcpPort", s.tcp_port);
    cJSON_AddBoolToObject(o, "udpEnabled", s.udp_enabled);
    cJSON_AddBoolToObject(o, "udpControl", s.udp_control);
    cJSON_AddBoolToObject(o, "udpAnnounce", s.udp_announce);
    cJSON_AddNumberToObject(o, "udpPort", s.udp_port);
    const std::string text = api::to_string(o);
    cJSON_Delete(o);
    {
        ModelLock lock(model_mutex());
        if (ctx_.config->save_document(kDoc, text) != ConfigStatus::kOk) {
            error = "Could not save";
            return ESP_FAIL;
        }
    }
    std::lock_guard<std::mutex> l(mutex_);
    settings_ = s;
    restart_ = true;
    return ESP_OK;
}

void ControlServers::start()
{
    if (started_) return;
    started_ = true;
    // Priority 3, 5 KB stack, core 0 (documented in docs/ARCHITECTURE.md).
    xTaskCreatePinnedToCore(&ControlServers::task_entry, "ctrl", 5120, this, 3, nullptr, 0);
}

void ControlServers::task_entry(void *arg)
{
    static_cast<ControlServers *>(arg)->run();
}

void ControlServers::open_sockets()
{
    const ControlSettings s = settings();
    if (s.tcp_enabled) {
        listen_fd_ = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
        if (listen_fd_ >= 0) {
            int one = 1;
            setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
            sockaddr_in addr{};
            addr.sin_family = AF_INET;
            addr.sin_port = htons(s.tcp_port);
            addr.sin_addr.s_addr = htonl(INADDR_ANY);
            if (bind(listen_fd_, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0 || listen(listen_fd_, 2) != 0) {
                ESP_LOGE(TAG, "TCP listen on %u failed", s.tcp_port);
                close(listen_fd_);
                listen_fd_ = -1;
            } else {
                ESP_LOGI(TAG, "TCP control on port %u", s.tcp_port);
            }
        }
    }
    if (s.udp_enabled) {
        udp_fd_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
        if (udp_fd_ >= 0) {
            int one = 1;
            setsockopt(udp_fd_, SOL_SOCKET, SO_BROADCAST, &one, sizeof(one));
            sockaddr_in addr{};
            addr.sin_family = AF_INET;
            addr.sin_port = htons(s.udp_port);
            addr.sin_addr.s_addr = htonl(INADDR_ANY);
            if (bind(udp_fd_, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0) {
                ESP_LOGE(TAG, "UDP bind on %u failed", s.udp_port);
                close(udp_fd_);
                udp_fd_ = -1;
            } else {
                ESP_LOGI(TAG, "UDP discovery%s on port %u", s.udp_control ? " + authenticated control" : "", s.udp_port);
            }
        }
    }
}

void ControlServers::close_sockets()
{
    for (auto &c : clients_) {
        if (c.fd >= 0) close(c.fd);
        c = Client{};
    }
    if (listen_fd_ >= 0) close(listen_fd_);
    if (udp_fd_ >= 0) close(udp_fd_);
    listen_fd_ = udp_fd_ = -1;
    std::lock_guard<std::mutex> l(mutex_);
    status_.tcp_clients = 0;
}

void ControlServers::send_line(Client &c, cJSON *obj)
{
    std::string text = api::to_string(obj);
    cJSON_Delete(obj);
    text.push_back('\n');
    if (send(c.fd, text.data(), text.size(), MSG_DONTWAIT) < 0) {
        close(c.fd);
        c = Client{};
    }
}

bool ControlServers::execute(const cJSON *cmd, const std::string &origin, std::string &error, cJSON *reply)
{
    const std::string type = api::str_or(cmd, "type", "");
    ModelLock lock(model_mutex());
    if (type == "light" || type == "group") {
        const std::string id = api::str_or(cmd, type == "light" ? "light_id" : "group_id", "");
        std::vector<std::string> ids;
        if (type == "light") {
            ids.push_back(id);
        } else {
            const Group *g = ctx_.groups->find(id);
            if (g == nullptr) {
                error = "unknown group";
                return false;
            }
            ids = g->channel_ids;
        }
        for (const auto &lid : ids) {
            const Channel *ch = ctx_.lighting->channel_manager().find(lid);
            if (ch == nullptr) {
                error = "unknown light '" + lid + "'";
                return false;
            }
            LightCommandEnvelope env;
            if (!integration::command_from_ha(cmd, ch->requested, env.command, error)) return false;
            env.command.channel_id = lid;
            env.command.origin = origin;
            if (ctx_.lighting->enqueue(env) != EnqueueResult::kOk) {
                error = "busy";
                return false;
            }
        }
        return true;
    }
    if (type == "scene") {
        const std::string id = api::str_or(cmd, "scene_id", "");
        if (ctx_.scenes->activate_scene(id, 0, origin) != SceneResult::kOk) {
            error = "unknown scene";
            return false;
        }
        return true;
    }
    if (type == "get") {
        const std::string id = api::str_or(cmd, "light_id", "");
        cJSON *arr = cJSON_AddArrayToObject(reply, "lights");
        const ChannelManager &cm = ctx_.lighting->channel_manager();
        for (std::size_t i = 0; i < cm.channel_count(); ++i) {
            if (!id.empty() && cm.at(i).id != id) continue;
            cJSON *st = integration::state_to_ha(cm.at(i), ctx_.lighting->active_effect(cm.at(i).id));
            cJSON_AddStringToObject(st, "light_id", cm.at(i).id.c_str());
            cJSON_AddItemToArray(arr, st);
        }
        return true;
    }
    if (type == "ping") {
        cJSON_AddStringToObject(reply, "type", "pong");
        return true;
    }
    error = "unknown command type";
    return false;
}

void ControlServers::handle_line(Client &c, const std::string &line)
{
    c.last_rx_ms = now_ms();
    cJSON *msg = cJSON_Parse(line.c_str());
    cJSON *reply = cJSON_CreateObject();
    cJSON_AddNumberToObject(reply, "v", 1);
    const cJSON *id = msg ? cJSON_GetObjectItemCaseSensitive(msg, "id") : nullptr;
    if (cJSON_IsNumber(id)) cJSON_AddNumberToObject(reply, "id", id->valuedouble);
    double v = 0;
    if (msg == nullptr || !api::get_number(msg, "v", v) || v != 1) {
        cJSON_AddBoolToObject(reply, "ok", false);
        cJSON_AddStringToObject(reply, "error", "expected JSON with \"v\":1");
        cJSON_Delete(msg);
        return send_line(c, reply);
    }
    const std::string type = api::str_or(msg, "type", "");
    if (!c.authed) {
        const bool ok = type == "auth" && ct_equal(api::str_or(msg, "token", ""), token());
        cJSON_AddStringToObject(reply, "type", "auth");
        cJSON_AddBoolToObject(reply, "ok", ok);
        cJSON_Delete(msg);
        if (!ok) {
            {
                std::lock_guard<std::mutex> l(mutex_);
                status_.tcp_auth_failures++;
            }
            send_line(c, reply);
            if (c.fd >= 0) close(c.fd);
            c = Client{};
            return;
        }
        c.authed = true;
        return send_line(c, reply);
    }
    std::string error;
    const bool ok = execute(msg, "tcp", error, reply);
    cJSON_Delete(msg);
    cJSON_AddBoolToObject(reply, "ok", ok);
    if (!ok) cJSON_AddStringToObject(reply, "error", error.c_str());
    {
        std::lock_guard<std::mutex> l(mutex_);
        status_.tcp_commands++;
    }
    send_line(c, reply);
}

void ControlServers::handle_udp(const uint8_t *data, std::size_t len, const void *from, unsigned from_len)
{
    const std::string text(reinterpret_cast<const char *>(data), len);
    cJSON *msg = cJSON_ParseWithLength(text.data(), text.size());
    const std::string type = msg ? api::str_or(msg, "type", "") : "";
    const ControlSettings s = settings();
    auto reply = [&](cJSON *o) {
        const std::string out = api::to_string(o);
        cJSON_Delete(o);
        sendto(udp_fd_, out.data(), out.size(), 0, static_cast<const sockaddr *>(from), from_len);
    };
    if (text == "LUMEN?" || type == "discover") {
        const NetworkStatus net = ctx_.network->status();
        cJSON *o = cJSON_CreateObject();
        cJSON_AddNumberToObject(o, "v", 1);
        cJSON_AddStringToObject(o, "type", "announce");
        cJSON_AddStringToObject(o, "id", net.hostname.c_str());
        cJSON_AddStringToObject(o, "ip", !net.sta_ip.empty() ? net.sta_ip.c_str() : net.eth_ip.c_str());
        cJSON_AddNumberToObject(o, "http", 80);
        if (s.tcp_enabled) cJSON_AddNumberToObject(o, "tcp", s.tcp_port);
        cJSON_AddBoolToObject(o, "udpControl", s.udp_control);
        cJSON_AddStringToObject(o, "version", kVersion);
        // Lumen Pro: lets the gateway and the app list controllers they can adopt.
        const std::string device = ctx_.cloud != nullptr ? ctx_.cloud->device_id() : std::string();
        cJSON_AddStringToObject(o, "cloud", device.empty() ? "off" : "paired");
        if (!device.empty()) cJSON_AddStringToObject(o, "device", device.c_str());
        reply(o);
        cJSON_Delete(msg);
        std::lock_guard<std::mutex> l(mutex_);
        status_.discovery_replies++;
        return;
    }
    auto rejected = [&](const char *why) {
        cJSON_Delete(msg);
        std::lock_guard<std::mutex> l(mutex_);
        status_.udp_rejected++;
        ESP_LOGW(TAG, "UDP frame rejected: %s", why);
    };
    if (!s.udp_control) return rejected("control disabled");
    const uint32_t now = now_ms();
    if (now - rate_window_ms_ >= 1000) {
        rate_window_ms_ = now;
        rate_count_ = 0;
    }
    if (++rate_count_ > kUdpRatePerSecond) return rejected("rate limit");
    double v = 0, ts = 0, seq = 0;
    const cJSON *cmd = msg ? cJSON_GetObjectItemCaseSensitive(msg, "cmd") : nullptr;
    const std::string src = msg ? api::str_or(msg, "src", "") : "";
    const std::string mac = msg ? api::str_or(msg, "mac", "") : "";
    if (msg == nullptr || !api::get_number(msg, "v", v) || v != 1 || !api::get_number(msg, "ts", ts) ||
        !api::get_number(msg, "seq", seq) || !cJSON_IsObject(cmd) || src.empty() || src.size() > 32) {
        return rejected("malformed");
    }
    const std::string canon = integration::canonical_frame(1, static_cast<int64_t>(ts), static_cast<uint32_t>(seq), src, cmd);
    const std::string tok = token();
    uint8_t expect[32];
    integration::hmac_sha256(reinterpret_cast<const uint8_t *>(tok.data()), tok.size(),
                             reinterpret_cast<const uint8_t *>(canon.data()), canon.size(), expect);
    if (!ct_equal(integration::hex(expect, 32), mac)) return rejected("bad signature");
    const int64_t wall = static_cast<int64_t>(std::time(nullptr));
    if (wall < 1700000000) return rejected("controller clock not synchronised");
    if (!replay_.accept(src, static_cast<uint32_t>(seq), static_cast<int64_t>(ts), wall)) return rejected("stale or replayed");
    std::string error;
    cJSON *ack = cJSON_CreateObject();
    cJSON_AddNumberToObject(ack, "v", 1);
    cJSON_AddNumberToObject(ack, "seq", seq);
    const bool ok = execute(cmd, "udp", error, ack);
    cJSON_AddBoolToObject(ack, "ok", ok);
    if (!ok) cJSON_AddStringToObject(ack, "error", error.c_str());
    reply(ack);
    cJSON_Delete(msg);
    std::lock_guard<std::mutex> l(mutex_);
    status_.udp_commands++;
}

void ControlServers::push_states()
{
    std::bitset<64> todo;
    {
        std::lock_guard<std::mutex> l(dirty_mutex_);
        todo = dirty_;
        dirty_.reset();
    }
    if (todo.none()) return;
    const ControlSettings s = settings();
    ModelLock lock(model_mutex());
    const ChannelManager &cm = ctx_.lighting->channel_manager();
    for (std::size_t i = 0; i < cm.channel_count(); ++i) {
        if (!todo.test(i)) continue;
        cJSON *st = integration::state_to_ha(cm.at(i), ctx_.lighting->active_effect(cm.at(i).id));
        cJSON_AddNumberToObject(st, "v", 1);
        cJSON_AddStringToObject(st, "type", "state");
        cJSON_AddStringToObject(st, "light_id", cm.at(i).id.c_str());
        const std::string text = api::to_string(st) + "\n";
        cJSON_Delete(st);
        for (auto &c : clients_) {
            if (c.fd >= 0 && c.authed && send(c.fd, text.data(), text.size(), MSG_DONTWAIT) < 0) {
                close(c.fd);
                c = Client{};
            }
        }
        if (s.udp_announce && udp_fd_ >= 0) {
            sockaddr_in bc{};
            bc.sin_family = AF_INET;
            bc.sin_port = htons(s.udp_port);
            bc.sin_addr.s_addr = htonl(INADDR_BROADCAST);
            sendto(udp_fd_, text.data(), text.size() - 1, 0, reinterpret_cast<sockaddr *>(&bc), sizeof(bc));
        }
    }
}

void ControlServers::run()
{
    open_sockets();
    char buf[512];
    const bool wdt = esp_task_wdt_add(nullptr) == ESP_OK; // select() wakes every 100 ms
    if (!wdt) ESP_LOGW(TAG, "ctrl task not on the task watchdog");
    for (;;) {
        if (wdt) esp_task_wdt_reset();
        bool restart = false;
        {
            std::lock_guard<std::mutex> l(mutex_);
            restart = restart_;
            restart_ = false;
        }
        if (restart) {
            close_sockets();
            open_sockets();
        }
        fd_set rfds;
        FD_ZERO(&rfds);
        int maxfd = -1;
        auto add = [&](int fd) {
            if (fd >= 0) {
                FD_SET(fd, &rfds);
                if (fd > maxfd) maxfd = fd;
            }
        };
        add(listen_fd_);
        add(udp_fd_);
        for (const auto &c : clients_) add(c.fd);
        timeval tv{0, 100000}; // 100 ms: also paces state pushes
        const int n = maxfd >= 0 ? select(maxfd + 1, &rfds, nullptr, nullptr, &tv) : 0;
        if (maxfd < 0) vTaskDelay(pdMS_TO_TICKS(500));
        const uint32_t now = now_ms();
        if (n > 0 && listen_fd_ >= 0 && FD_ISSET(listen_fd_, &rfds)) {
            const int fd = accept(listen_fd_, nullptr, nullptr);
            if (fd >= 0) {
                Client *slot = nullptr;
                for (auto &c : clients_) {
                    if (c.fd < 0) {
                        slot = &c;
                        break;
                    }
                }
                if (slot == nullptr) {
                    const char *busy = "{\"v\":1,\"ok\":false,\"error\":\"too many clients\"}\n";
                    send(fd, busy, std::strlen(busy), MSG_DONTWAIT);
                    close(fd);
                } else {
                    // Detect peers that vanish without a FIN (power loss, Wi-Fi
                    // drop) in ~45 s instead of holding a slot for the idle timeout.
                    int on = 1, idle = 30, intvl = 5, cnt = 3;
                    setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &on, sizeof(on));
                    setsockopt(fd, IPPROTO_TCP, TCP_KEEPIDLE, &idle, sizeof(idle));
                    setsockopt(fd, IPPROTO_TCP, TCP_KEEPINTVL, &intvl, sizeof(intvl));
                    setsockopt(fd, IPPROTO_TCP, TCP_KEEPCNT, &cnt, sizeof(cnt));
                    *slot = Client{};
                    slot->fd = fd;
                    slot->connected_ms = now;
                    slot->last_rx_ms = now;
                }
            }
        }
        if (n > 0 && udp_fd_ >= 0 && FD_ISSET(udp_fd_, &rfds)) {
            sockaddr_storage from{};
            socklen_t flen = sizeof(from);
            const int r = recvfrom(udp_fd_, buf, sizeof(buf), 0, reinterpret_cast<sockaddr *>(&from), &flen);
            if (r > 0) handle_udp(reinterpret_cast<uint8_t *>(buf), static_cast<std::size_t>(r), &from, flen);
        }
        uint8_t count = 0;
        for (auto &c : clients_) {
            if (c.fd < 0) continue;
            if (n > 0 && FD_ISSET(c.fd, &rfds)) {
                const int r = recv(c.fd, buf, sizeof(buf), 0);
                std::vector<std::string> lines;
                if (r <= 0 || !c.framer.feed(buf, static_cast<std::size_t>(r), lines)) {
                    close(c.fd);
                    c = Client{};
                    continue;
                }
                for (const auto &line : lines) {
                    if (c.fd < 0) break;
                    handle_line(c, line);
                }
            }
            // Fresh timestamp: handle_line() may have set last_rx_ms after `now`
            // was taken, and unsigned (now - last_rx) would then wrap.
            const uint32_t t = now_ms();
            if (c.fd >= 0 && ((!c.authed && t - c.connected_ms > kAuthTimeoutMs) || t - c.last_rx_ms > kIdleTimeoutMs)) {
                close(c.fd);
                c = Client{};
            }
            if (c.fd >= 0) ++count;
        }
        {
            std::lock_guard<std::mutex> l(mutex_);
            status_.tcp_clients = count;
        }
        push_states();
    }
}

} // namespace lumen
