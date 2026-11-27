// Lumen network manager (SPEC §15).
//
// Wi-Fi station + access point with a configurable fallback policy,
// optional Ethernet (board-profile driven), mDNS and SNTP.
//
// Security (SPEC §15.2 / AGENTS.md §8): the setup AP is always WPA2-PSK
// with a per-device random password generated on first boot and stored in
// NVS — never an open management hotspot. The password is printed on the
// serial console at boot and shown in the authenticated Network screen.
//
// Credentials live in NVS (namespace "net"), never in JSON config or
// exports. Lighting and local automation keep running without any network.
#pragma once

#include <cstdint>
#include <mutex>
#include <string>

#include "esp_err.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "lumen/hardware/board_profile.h"

namespace lumen
{

enum class WifiPolicy : uint8_t
{
    kStationFallbackAp = 0, // STA; AP only while STA is unconfigured/unreachable
    kStationOnly,
    kApOnly,
    kStationAndAp,
};

const char *wifi_policy_name(WifiPolicy p);
bool wifi_policy_from_string(const std::string &s, WifiPolicy &out);

struct StaticIpConfig
{
    bool enabled = false;
    std::string ip, netmask, gateway, dns;
};

struct NetworkSettings
{
    std::string hostname;      // mDNS + DHCP hostname ("lumen-xxxx")
    std::string sta_ssid;
    std::string sta_password;  // secret: never serialised to the API
    StaticIpConfig sta_static;
    WifiPolicy policy = WifiPolicy::kStationFallbackAp;
    std::string ap_ssid;       // "Lumen-XXXX"
    std::string ap_password;   // secret (shown only to authenticated users)
    uint8_t ap_channel = 6;
    bool ethernet_enabled = false; // opt-in (ESP32 RMII clock-out + Wi-Fi errata)
    std::string timezone = "UTC0"; // POSIX TZ string
    std::string ntp_server = "pool.ntp.org";
};

struct NetworkStatus
{
    bool sta_configured = false;
    bool sta_connected = false;
    std::string sta_ssid;
    std::string sta_ip, sta_gateway, sta_netmask;
    int8_t rssi = 0;
    bool ap_active = false;
    std::string ap_ssid;
    std::string ap_ip;
    uint8_t ap_clients = 0;
    bool eth_present = false;
    bool eth_link = false;
    std::string eth_ip;
    std::string hostname;
    std::string mac;
    bool time_synced = false;
    uint32_t sta_reconnects = 0;
};

class NetworkManager
{
  public:
    using StatusListener = void (*)(void *user);

    NetworkManager() = default;

    // Loads settings (seeding defaults/dev credentials), starts Wi-Fi per
    // policy and Ethernet when the profile declares it and it is enabled.
    esp_err_t start(const BoardProfile &profile);

    // Called once per second from the housekeeping task: fallback policy,
    // AP shutdown after STA has been stable, reconnect backoff.
    void poll();

    NetworkStatus status() const;
    NetworkSettings settings() const; // includes secrets: callers must redact

    // Apply + persist new settings. Changing STA credentials reconnects.
    esp_err_t update_wifi(const std::string &ssid, const std::string *password,
                          const StaticIpConfig &static_ip);
    esp_err_t update_ap(const std::string *ssid, const std::string *password, WifiPolicy policy,
                        uint8_t channel);
    esp_err_t update_system(const std::string *hostname, const std::string *timezone,
                            const std::string *ntp_server, const bool *ethernet_enabled);

    // Scan for nearby networks (blocking ~2 s; call from the API task).
    std::string scan_json();

    void set_listener(StatusListener l, void *user)
    {
        listener_ = l;
        listener_user_ = user;
    }

    // Lumen Pro: "cloud" TXT item of the _lumen._tcp service ("off" | "paired").
    void set_cloud_txt(const char *state);

  private:
    static void on_event(void *arg, esp_event_base_t base, int32_t id, void *data);
    void handle_event(esp_event_base_t base, int32_t id, void *data);
    esp_err_t load_settings();
    esp_err_t save_settings() const;
    esp_err_t apply_wifi_mode();
    void start_ap_locked(bool on);
    esp_err_t start_ethernet(const BoardProfile &profile);
    void start_mdns();
    void start_sntp();
    void notify();

    mutable std::recursive_mutex mutex_;
    NetworkSettings settings_;
    NetworkStatus status_;
    esp_netif_t *sta_netif_ = nullptr;
    esp_netif_t *ap_netif_ = nullptr;
    esp_netif_t *eth_netif_ = nullptr;
    void *eth_handle_ = nullptr;
    bool wifi_started_ = false;
    bool ap_on_ = false;
    uint32_t sta_down_since_ms_ = 0;
    uint32_t sta_up_since_ms_ = 0;
    uint32_t next_retry_ms_ = 0;
    uint32_t retry_delay_ms_ = 1000;
    bool mdns_started_ = false;
    std::string cloud_txt_ = "off";
    StatusListener listener_ = nullptr;
    void *listener_user_ = nullptr;
};

} // namespace lumen
