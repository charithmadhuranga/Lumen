// Lumen — outbound UDP/TCP sender. See net_sender.hpp
#include "lumen/networking/net_sender.hpp"

#include <cstring>

#include "esp_log.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"

namespace lumen
{

namespace
{
const char *TAG = "netsend";
}

bool NetSender::enqueue(const std::string &host, uint16_t port, const std::string &payload, bool tcp)
{
    if (host.empty() || host.size() > 63 || port == 0 || payload.size() > kMaxPayload) return false;
    std::lock_guard<std::mutex> lock(mutex_);
    if (count_ >= kQueue) {
        ++dropped_;
        return false;
    }
    queue_[(head_ + count_) % kQueue] = {host, port, payload, tcp};
    ++count_;
    return true;
}

bool NetSender::send_one(const Item &item)
{
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = item.tcp ? SOCK_STREAM : SOCK_DGRAM;
    addrinfo *res = nullptr;
    const std::string port = std::to_string(item.port);
    if (getaddrinfo(item.host.c_str(), port.c_str(), &hints, &res) != 0 || res == nullptr) {
        ESP_LOGW(TAG, "cannot resolve %s", item.host.c_str());
        return false;
    }
    const int fd = socket(res->ai_family, res->ai_socktype, 0);
    bool ok = false;
    if (fd >= 0) {
        timeval tv{2, 0};
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        if (item.tcp) {
            ok = connect(fd, res->ai_addr, res->ai_addrlen) == 0 &&
                 send(fd, item.payload.data(), item.payload.size(), 0) == static_cast<int>(item.payload.size());
        } else {
            ok = sendto(fd, item.payload.data(), item.payload.size(), 0, res->ai_addr, res->ai_addrlen) ==
                 static_cast<int>(item.payload.size());
        }
        close(fd);
    }
    freeaddrinfo(res);
    if (!ok) ESP_LOGW(TAG, "%s send to %s:%u failed", item.tcp ? "TCP" : "UDP", item.host.c_str(), item.port);
    return ok;
}

void NetSender::process()
{
    for (;;) {
        Item item;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (count_ == 0) return;
            item = queue_[head_];
            head_ = (head_ + 1) % kQueue;
            --count_;
        }
        if (send_one(item)) ++sent_;
        else ++failed_;
    }
}

} // namespace lumen
