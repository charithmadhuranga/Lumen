// Lumen WebSocket manager implementation. See websocket_manager.hpp
#include "lumen/web/websocket_manager.hpp"

#include <cstring>
#include <ctime>
#include <vector>

#include "esp_log.h"
#include "esp_timer.h"

namespace lumen
{

namespace
{
const char *TAG = "ws";

int64_t event_timestamp()
{
    // Unix seconds once SNTP has set the clock, else uptime seconds.
    const std::time_t now = std::time(nullptr);
    if (now > 1700000000) {
        return static_cast<int64_t>(now);
    }
    return esp_timer_get_time() / 1000000;
}
} // namespace

esp_err_t WebSocketManager::on_open(httpd_req_t *req, const std::string &session_token)
{
    const int fd = httpd_req_to_sockfd(req);
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto &c : clients_) {
        if (c.fd == fd) {
            c.token = session_token;
            return ESP_OK;
        }
    }
    for (auto &c : clients_) {
        if (c.fd < 0) {
            c.fd = fd;
            c.token = session_token;
            ESP_LOGI(TAG, "client fd=%d connected", fd);
            return ESP_OK;
        }
    }
    ESP_LOGW(TAG, "client limit reached; rejecting fd=%d", fd);
    return ESP_FAIL;
}

esp_err_t WebSocketManager::on_frame(httpd_req_t *req)
{
    // Clients only send pings/keep-alives; payloads are read and discarded
    // (bounded) so the socket stays healthy. Control goes through REST.
    httpd_ws_frame_t frame{};
    esp_err_t err = httpd_ws_recv_frame(req, &frame, 0);
    if (err != ESP_OK) {
        return err;
    }
    if (frame.len > 0 && frame.len <= 512) {
        std::vector<uint8_t> buf(frame.len + 1);
        frame.payload = buf.data();
        err = httpd_ws_recv_frame(req, &frame, frame.len);
        if (err == ESP_OK && frame.type == HTTPD_WS_TYPE_TEXT &&
            std::strncmp(reinterpret_cast<char *>(buf.data()), "{\"type\":\"ping\"", 14) == 0) {
            static const char pong[] = "{\"version\":1,\"event\":\"pong\",\"timestamp\":0,\"data\":{}}";
            httpd_ws_frame_t out{};
            out.type = HTTPD_WS_TYPE_TEXT;
            out.payload = reinterpret_cast<uint8_t *>(const_cast<char *>(pong));
            out.len = sizeof(pong) - 1;
            httpd_ws_send_frame(req, &out);
        }
    } else if (frame.len > 512) {
        return ESP_FAIL; // oversized client frame: drop the connection
    }
    if (frame.type == HTTPD_WS_TYPE_CLOSE) {
        const int fd = httpd_req_to_sockfd(req);
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto &c : clients_) {
            if (c.fd == fd) c = Client{};
        }
    }
    return ESP_OK;
}

std::string WebSocketManager::envelope(const char *event, const std::string &data_json) const
{
    std::string out;
    out.reserve(data_json.size() + 64);
    out += "{\"version\":1,\"event\":\"";
    out += event;
    out += "\",\"timestamp\":";
    out += std::to_string(event_timestamp());
    out += ",\"data\":";
    out += data_json.empty() ? "{}" : data_json;
    out += "}";
    return out;
}

void WebSocketManager::notify_light(std::size_t index)
{
    if (index >= kMaxLights) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        dirty_lights_.set(index);
    }
    schedule_flush();
}

void WebSocketManager::broadcast(const char *event, const std::string &data_json, bool droppable)
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        bool any = false;
        for (const auto &c : clients_) any |= c.fd >= 0;
        if (!any) {
            return; // nobody listening: nothing to queue
        }
        if (q_count_ == kQueueSize) {
            // Drop the oldest droppable event; a non-droppable one only
            // displaces telemetry, otherwise it is counted as dropped.
            std::size_t victim = kQueueSize;
            for (std::size_t i = 0; i < q_count_; ++i) {
                const std::size_t idx = (q_head_ + i) % kQueueSize;
                if (queue_[idx].droppable) {
                    victim = i;
                    break;
                }
            }
            ++dropped_;
            if (victim == kQueueSize) {
                return;
            }
            for (std::size_t i = victim; i + 1 < q_count_; ++i) {
                queue_[(q_head_ + i) % kQueueSize] = std::move(queue_[(q_head_ + i + 1) % kQueueSize]);
            }
            --q_count_;
        }
        Pending &p = queue_[(q_head_ + q_count_) % kQueueSize];
        p.text = envelope(event, data_json);
        p.droppable = droppable;
        ++q_count_;
    }
    schedule_flush();
}

void WebSocketManager::schedule_flush()
{
    if (server_ == nullptr) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (flush_scheduled_) {
            return;
        }
        flush_scheduled_ = true;
    }
    if (httpd_queue_work(server_, flush_work, this) != ESP_OK) {
        std::lock_guard<std::mutex> lock(mutex_);
        flush_scheduled_ = false;
    }
}

void WebSocketManager::flush_work(void *arg)
{
    static_cast<WebSocketManager *>(arg)->flush();
}

void WebSocketManager::send_all(const std::string &text)
{
    std::array<int, kMaxClients> fds{};
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (std::size_t i = 0; i < kMaxClients; ++i) fds[i] = clients_[i].fd;
    }
    for (std::size_t i = 0; i < kMaxClients; ++i) {
        const int fd = fds[i];
        if (fd < 0) continue;
        if (httpd_ws_get_fd_info(server_, fd) != HTTPD_WS_CLIENT_WEBSOCKET) {
            std::lock_guard<std::mutex> lock(mutex_);
            if (clients_[i].fd == fd) clients_[i] = Client{};
            continue;
        }
        httpd_ws_frame_t frame{};
        frame.type = HTTPD_WS_TYPE_TEXT;
        frame.payload = reinterpret_cast<uint8_t *>(const_cast<char *>(text.data()));
        frame.len = text.size();
        if (httpd_ws_send_frame_async(server_, fd, &frame) != ESP_OK) {
            ESP_LOGW(TAG, "send failed on fd=%d; closing", fd);
            httpd_sess_trigger_close(server_, fd);
            std::lock_guard<std::mutex> lock(mutex_);
            if (clients_[i].fd == fd) clients_[i] = Client{};
        } else {
            ++sent_;
        }
    }
}

void WebSocketManager::flush()
{
    std::bitset<kMaxLights> lights;
    std::vector<std::string> events;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        flush_scheduled_ = false;
        lights = dirty_lights_;
        dirty_lights_.reset();
        events.reserve(q_count_);
        while (q_count_ > 0) {
            events.push_back(std::move(queue_[q_head_].text));
            q_head_ = (q_head_ + 1) % kQueueSize;
            --q_count_;
        }
    }
    for (auto &e : events) {
        send_all(e);
    }
    if (lights.any() && render_light_) {
        for (std::size_t i = 0; i < kMaxLights; ++i) {
            if (!lights.test(i)) continue;
            const std::string data = render_light_(i);
            if (!data.empty()) {
                send_all(envelope("light_state_changed", data));
            }
        }
    }
}

void WebSocketManager::revalidate_sessions()
{
    if (!session_check_ || server_ == nullptr) {
        return;
    }
    std::array<Client, kMaxClients> snapshot;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        snapshot = clients_;
    }
    for (std::size_t i = 0; i < kMaxClients; ++i) {
        if (snapshot[i].fd >= 0 && !session_check_(snapshot[i].token)) {
            httpd_sess_trigger_close(server_, snapshot[i].fd);
            std::lock_guard<std::mutex> lock(mutex_);
            if (clients_[i].fd == snapshot[i].fd) clients_[i] = Client{};
        }
    }
}

std::size_t WebSocketManager::client_count() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    std::size_t n = 0;
    for (const auto &c : clients_) n += c.fd >= 0 ? 1 : 0;
    return n;
}

} // namespace lumen
