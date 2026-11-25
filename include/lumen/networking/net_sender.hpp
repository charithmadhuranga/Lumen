// Lumen — outbound UDP/TCP sender for automation actions (SPEC §11.4,
// §14). Producers enqueue (bounded, non-blocking); the housekeeping task
// drains the queue with short socket timeouts so a dead host can never
// stall lighting or the HTTP server.
#pragma once

#include <array>
#include <cstdint>
#include <mutex>
#include <string>

namespace lumen
{

class NetSender
{
  public:
    static constexpr std::size_t kQueue = 8;
    static constexpr std::size_t kMaxPayload = 512;

    bool enqueue(const std::string &host, uint16_t port, const std::string &payload, bool tcp);
    // Sends everything queued (call from a low-priority task).
    void process();
    uint32_t sent() const { return sent_; }
    uint32_t failed() const { return failed_; }
    uint32_t dropped() const { return dropped_; }

  private:
    struct Item
    {
        std::string host;
        uint16_t port = 0;
        std::string payload;
        bool tcp = false;
    };
    bool send_one(const Item &item);

    std::mutex mutex_;
    std::array<Item, kQueue> queue_{};
    std::size_t head_ = 0;
    std::size_t count_ = 0;
    uint32_t sent_ = 0;
    uint32_t failed_ = 0;
    uint32_t dropped_ = 0;
};

} // namespace lumen
