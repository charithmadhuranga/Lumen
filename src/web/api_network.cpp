// Lumen API — networking (SPEC §15, §17.2).
//
//   GET /network                 settings (secrets redacted)
//   GET /network/status          live link state
//   GET /network/scan            nearby Wi-Fi networks
//   GET /network/ap-password     setup AP password (authenticated only)
//   PUT /network/wifi            {ssid, password?, staticIp?}
//   PUT /network/ap              {ssid?, password?, policy, channel?}
//   PUT /network/ethernet        {enabled}           (applies after restart)
//   PUT /network/system          {hostname?, timezone?, ntpServer?}
//
// Changes that can cut the client's connection are applied ~0.5 s after
// the response has been sent.
#include <functional>
#include <memory>

#include "esp_timer.h"
#include "lumen/app/app_context.hpp"
#include "lumen/web/api_util.hpp"

namespace lumen
{

namespace
{

void defer(std::function<void()> fn, uint32_t delay_ms)
{
    auto *heap_fn = new std::function<void()>(std::move(fn));
    esp_timer_create_args_t args{};
    args.callback = [](void *arg) {
        std::unique_ptr<std::function<void()>> f(static_cast<std::function<void()> *>(arg));
        (*f)();
    };
    args.arg = heap_fn;
    args.name = "net_defer";
    esp_timer_handle_t t = nullptr;
    if (esp_timer_create(&args, &t) != ESP_OK || esp_timer_start_once(t, delay_ms * 1000ULL) != ESP_OK) {
        (*heap_fn)();
        delete heap_fn;
    }
    // The one-shot timer handle is intentionally not deleted from inside its
    // own callback; a few bytes per settings change is acceptable.
}

cJSON *settings_json(const NetworkSettings &s, bool eth_present)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "hostname", s.hostname.c_str());
    cJSON *wifi = cJSON_AddObjectToObject(o, "wifi");
    cJSON_AddStringToObject(wifi, "ssid", s.sta_ssid.c_str());
    cJSON_AddBoolToObject(wifi, "hasPassword", !s.sta_password.empty());
    cJSON *st = cJSON_AddObjectToObject(wifi, "staticIp");
    cJSON_AddBoolToObject(st, "enabled", s.sta_static.enabled);
    cJSON_AddStringToObject(st, "ip", s.sta_static.ip.c_str());
    cJSON_AddStringToObject(st, "netmask", s.sta_static.netmask.c_str());
    cJSON_AddStringToObject(st, "gateway", s.sta_static.gateway.c_str());
    cJSON_AddStringToObject(st, "dns", s.sta_static.dns.c_str());
    cJSON *ap = cJSON_AddObjectToObject(o, "ap");
    cJSON_AddStringToObject(ap, "ssid", s.ap_ssid.c_str());
    cJSON_AddStringToObject(ap, "policy", wifi_policy_name(s.policy));
    cJSON_AddNumberToObject(ap, "channel", s.ap_channel);
    cJSON_AddStringToObject(ap, "security", "wpa2");
    cJSON *eth = cJSON_AddObjectToObject(o, "ethernet");
    cJSON_AddBoolToObject(eth, "present", eth_present);
    cJSON_AddBoolToObject(eth, "enabled", s.ethernet_enabled);
    cJSON_AddStringToObject(o, "timezone", s.timezone.c_str());
    cJSON_AddStringToObject(o, "ntpServer", s.ntp_server.c_str());
    return o;
}

} // namespace

cJSON *network_status_json(const NetworkStatus &n)
{
    cJSON *o = cJSON_CreateObject();
    cJSON *sta = cJSON_AddObjectToObject(o, "wifi");
    cJSON_AddBoolToObject(sta, "configured", n.sta_configured);
    cJSON_AddBoolToObject(sta, "connected", n.sta_connected);
    cJSON_AddStringToObject(sta, "ssid", n.sta_ssid.c_str());
    cJSON_AddStringToObject(sta, "ip", n.sta_ip.c_str());
    cJSON_AddStringToObject(sta, "gateway", n.sta_gateway.c_str());
    cJSON_AddStringToObject(sta, "netmask", n.sta_netmask.c_str());
    cJSON_AddNumberToObject(sta, "rssi", n.rssi);
    cJSON_AddNumberToObject(sta, "reconnects", n.sta_reconnects);
    cJSON *ap = cJSON_AddObjectToObject(o, "ap");
    cJSON_AddBoolToObject(ap, "active", n.ap_active);
    cJSON_AddStringToObject(ap, "ssid", n.ap_ssid.c_str());
    cJSON_AddStringToObject(ap, "ip", n.ap_ip.c_str());
    cJSON_AddNumberToObject(ap, "clients", n.ap_clients);
    cJSON *eth = cJSON_AddObjectToObject(o, "ethernet");
    cJSON_AddBoolToObject(eth, "present", n.eth_present);
    cJSON_AddBoolToObject(eth, "link", n.eth_link);
    cJSON_AddStringToObject(eth, "ip", n.eth_ip.c_str());
    cJSON_AddStringToObject(o, "hostname", n.hostname.c_str());
    cJSON_AddStringToObject(o, "mac", n.mac.c_str());
    cJSON_AddBoolToObject(o, "timeSynced", n.time_synced);
    return o;
}

void register_network_routes(AppContext &ctx)
{
    WebServer &web = *ctx.web;

    web.route(HTTP_GET, "/network", RouteAuth::kUser, [&ctx](ApiRequest &req) {
        return send_data(req, settings_json(ctx.network->settings(), ctx.network->status().eth_present));
    });

    web.route(HTTP_GET, "/network/status", RouteAuth::kUser, [&ctx](ApiRequest &req) {
        return send_data(req, network_status_json(ctx.network->status()));
    });

    web.route(HTTP_GET, "/network/scan", RouteAuth::kUser, [&ctx](ApiRequest &req) {
        cJSON *arr = cJSON_Parse(ctx.network->scan_json().c_str());
        return send_data(req, arr != nullptr ? arr : cJSON_CreateArray());
    });

    web.route(HTTP_GET, "/network/ap-password", RouteAuth::kUser, [&ctx](ApiRequest &req) {
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "password", ctx.network->settings().ap_password.c_str());
        return send_data(req, o);
    });

    web.route(HTTP_PUT, "/network/wifi", RouteAuth::kUser, [&ctx](ApiRequest &req) {
        cJSON *body = req.json();
        std::string ssid;
        if (!api::get_string(body, "ssid", ssid, 33) || ssid.size() > 32) {
            return send_error(req, 400, "INVALID_REQUEST", "ssid (1-32 chars, empty to disable) required");
        }
        std::string password;
        const bool has_pw = api::get_string(body, "password", password, 64);
        if (has_pw && !password.empty() && (password.size() < 8 || password.size() > 63)) {
            return send_error(req, 400, "INVALID_REQUEST", "Wi-Fi password must be 8-63 characters");
        }
        StaticIpConfig st;
        const cJSON *sip = api::field(body, "staticIp");
        if (cJSON_IsObject(sip)) {
            api::get_bool(sip, "enabled", st.enabled);
            api::get_string(sip, "ip", st.ip, 15);
            api::get_string(sip, "netmask", st.netmask, 15);
            api::get_string(sip, "gateway", st.gateway, 15);
            api::get_string(sip, "dns", st.dns, 15);
        }
        NetworkManager *net = ctx.network;
        cJSON *o = cJSON_CreateObject();
        cJSON_AddBoolToObject(o, "applying", true);
        const esp_err_t err = send_data(req, o, 202);
        defer([net, ssid, has_pw, password, st]() {
            net->update_wifi(ssid, has_pw ? &password : nullptr, st);
        }, 500);
        return err;
    });

    web.route(HTTP_PUT, "/network/ap", RouteAuth::kUser, [&ctx](ApiRequest &req) {
        cJSON *body = req.json();
        const NetworkSettings cur = ctx.network->settings();
        WifiPolicy policy = cur.policy;
        std::string s;
        if (api::get_string(body, "policy", s, 20) && !wifi_policy_from_string(s, policy)) {
            return send_error(req, 400, "INVALID_REQUEST",
                              "policy must be sta_fallback_ap, sta_only, ap_only or sta_and_ap");
        }
        std::string ssid, password;
        const bool has_ssid = api::get_string(body, "ssid", ssid, 33);
        const bool has_pw = api::get_string(body, "password", password, 64);
        if (has_ssid && (ssid.empty() || ssid.size() > 32)) return send_error(req, 400, "INVALID_REQUEST", "ssid 1-32 chars");
        if (has_pw && (password.size() < 8 || password.size() > 63))
            return send_error(req, 400, "INVALID_REQUEST", "AP password must be 8-63 characters (WPA2)");
        double ch = cur.ap_channel;
        api::get_number(body, "channel", ch);
        if (ch < 1 || ch > 13) return send_error(req, 400, "INVALID_REQUEST", "channel 1-13");
        if (policy == WifiPolicy::kStationOnly && cur.sta_ssid.empty()) {
            return send_error(req, 400, "INVALID_CONFIGURATION",
                              "Configure a Wi-Fi network before disabling the access point");
        }
        NetworkManager *net = ctx.network;
        const auto channel = static_cast<uint8_t>(ch);
        cJSON *o = cJSON_CreateObject();
        cJSON_AddBoolToObject(o, "applying", true);
        const esp_err_t err = send_data(req, o, 202);
        defer([net, has_ssid, ssid, has_pw, password, policy, channel]() {
            net->update_ap(has_ssid ? &ssid : nullptr, has_pw ? &password : nullptr, policy, channel);
        }, 500);
        return err;
    });

    web.route(HTTP_PUT, "/network/ethernet", RouteAuth::kUser, [&ctx](ApiRequest &req) {
        bool enabled = false;
        if (!api::get_bool(req.json(), "enabled", enabled)) {
            return send_error(req, 400, "INVALID_REQUEST", "enabled (boolean) required");
        }
        if (ctx.network->update_system(nullptr, nullptr, nullptr, &enabled) != ESP_OK) {
            return send_error(req, 500, "STORAGE_ERROR", "Could not save");
        }
        cJSON *o = cJSON_CreateObject();
        cJSON_AddBoolToObject(o, "enabled", enabled);
        cJSON_AddBoolToObject(o, "restartRequired", true);
        return send_data(req, o);
    });

    web.route(HTTP_PUT, "/network/system", RouteAuth::kUser, [&ctx](ApiRequest &req) {
        cJSON *body = req.json();
        std::string hostname, tz, ntp;
        const bool h = api::get_string(body, "hostname", hostname, 32);
        const bool t = api::get_string(body, "timezone", tz, 48);
        const bool n = api::get_string(body, "ntpServer", ntp, 63);
        const esp_err_t err = ctx.network->update_system(h ? &hostname : nullptr, t ? &tz : nullptr,
                                                         n ? &ntp : nullptr, nullptr);
        if (err == ESP_ERR_INVALID_ARG) {
            return send_error(req, 400, "INVALID_REQUEST",
                              "hostname: a-z, 0-9, '-' (max 31); timezone: POSIX TZ string");
        }
        if (err != ESP_OK) return send_error(req, 500, "STORAGE_ERROR", "Could not save");
        return send_data(req, settings_json(ctx.network->settings(), ctx.network->status().eth_present));
    });
}

} // namespace lumen
