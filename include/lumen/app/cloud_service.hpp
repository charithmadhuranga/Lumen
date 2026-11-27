// Lumen — the controller's Lumen Cloud link (Lumen Pro P5; DECISIONS D-17).
//
// Holds the pairing (config/cloud.json) and its secrets (NVS namespace
// "cloud": K_local, last known time), verifies local tickets for the web
// server, and runs the pairing request to the gateway on the housekeeping
// task. The controller never connects to the cloud itself.
//
// Thread safety: verify() runs on the httpd task, poll() on housekeeping,
// the route handlers on httpd; one mutex guards all state. The HTTP call to
// the gateway runs outside the mutex.
#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>

#include "esp_err.h"
#include "lumen/services/cloud_link.hpp"
#include "lumen/services/ticket.hpp"

struct cJSON;

namespace lumen
{

class ConfigManager;
class NetworkManager;
class BoardService;

class CloudService
{
  public:
    enum class State : uint8_t
    {
        kOff,
        kPairing,
        kPaired,
        kPairFailed,
    };

    struct Verdict
    {
        bool ok = false;
        bool admin = false;     // scope "admin"
        bool gateway = false;   // subject "g:..." (the paired gateway)
        ticket::Reason reason = ticket::Reason::kFormat;
    };

    CloudService(ConfigManager &config, NetworkManager &network, BoardService &boards);

    // Boot: load cloud.json and the NVS secrets. A paired document without its
    // key (e.g. NVS erased) is treated as unpaired and reported.
    esp_err_t init();

    Verdict verify(const std::string &ticket);

    // Validates and queues a pairing request; the housekeeping task runs it.
    bool request_pair(const std::string &claim_token, const std::string &gateway_url, std::string &error);
    esp_err_t unpair();
    // Epochs only increase; raising one revokes every older ticket.
    bool raise_epoch(uint32_t epoch, std::string &error);

    // Housekeeping (250 ms): pending pairing, last-known-time persistence.
    void poll(uint32_t uptime_ms);
    void on_shutdown();

    cJSON *status_json(); // caller owns
    bool paired();
    std::string device_id();

    // Called (outside the mutex) after pairing state changes: discovery updates.
    void set_on_change(std::function<void()> cb) { on_change_ = std::move(cb); }

  private:
    void run_pairing(const std::string &claim, const std::string &url);
    bool save_settings(const cloud::Settings &s);
    void persist_last_time(bool force);
    int64_t ticket_now();
    void notify();

    ConfigManager &config_;
    NetworkManager &network_;
    BoardService &boards_;
    std::mutex mutex_;
    cloud::Settings settings_;
    std::array<uint8_t, 32> key_{};
    bool have_key_ = false;
    State state_ = State::kOff;
    std::string last_error_;
    // Pending pairing job (one at a time).
    bool job_ = false;
    std::string job_claim_, job_url_;
    // Last known wall time (NVS) and when it was last written.
    int64_t last_time_ = 0;
    int64_t last_time_saved_ = 0;
    uint32_t last_persist_ms_ = 0;
    // Counters for diagnostics.
    uint32_t accepted_ = 0, rejected_ = 0;
    uint32_t rejected_by_reason_[9] = {};
    uint32_t last_gateway_ticket_s_ = 0; // uptime of the gateway's last accepted ticket (0 = never)
    std::function<void()> on_change_;
};

} // namespace lumen
