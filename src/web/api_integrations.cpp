// Lumen API — integrations (SPEC §13–14, §17.2).
//
//   GET  /integrations                    MQTT/HA + TCP/UDP settings and status
//   PUT  /integrations/mqtt               broker settings (password write-only)
//   POST /integrations/mqtt/test          current connection status
//   PUT  /integrations/homeassistant      {enabled, prefix}
//   PUT  /integrations/tcp | /udp         control server settings
//   GET  /integrations/token              integration token (authenticated UI)
//   POST /integrations/token/regenerate
// Secrets are never returned except the integration token to a signed-in
// user who explicitly asks for it; the MQTT password is write-only.
#include "lumen/app/app_context.hpp"
#include "lumen/networking/control_servers.hpp"
#include "lumen/networking/mqtt_service.hpp"
#include "lumen/web/api_util.hpp"

namespace lumen
{

namespace
{

cJSON *mqtt_json(const MqttSettings &s, const MqttStatus &st)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddBoolToObject(o, "enabled", s.enabled);
    cJSON_AddStringToObject(o, "host", s.host.c_str());
    cJSON_AddNumberToObject(o, "port", s.port);
    cJSON_AddBoolToObject(o, "tls", s.tls);
    cJSON_AddStringToObject(o, "username", s.username.c_str());
    cJSON_AddBoolToObject(o, "hasPassword", st.has_password);
    cJSON_AddStringToObject(o, "clientId", s.client_id.c_str());
    cJSON_AddStringToObject(o, "topicPrefix", s.topic_prefix.c_str());
    cJSON_AddStringToObject(o, "controllerId", s.controller_id.c_str());
    cJSON_AddNumberToObject(o, "qos", s.qos);
    cJSON_AddNumberToObject(o, "keepalive", s.keepalive);
    cJSON_AddBoolToObject(o, "retain", s.retain);
    cJSON *ha = cJSON_AddObjectToObject(o, "homeAssistant");
    cJSON_AddBoolToObject(ha, "enabled", s.ha_discovery);
    cJSON_AddStringToObject(ha, "prefix", s.ha_prefix.c_str());
    cJSON *status = cJSON_AddObjectToObject(o, "status");
    cJSON_AddBoolToObject(status, "connected", st.connected);
    cJSON_AddStringToObject(status, "broker", st.broker.c_str());
    cJSON_AddStringToObject(status, "lastError", st.last_error.c_str());
    cJSON_AddNumberToObject(status, "published", st.published);
    cJSON_AddNumberToObject(status, "received", st.received);
    cJSON_AddNumberToObject(status, "rejected", st.rejected);
    cJSON_AddNumberToObject(status, "reconnects", st.reconnects);
    cJSON_AddNumberToObject(status, "dropped", st.dropped);
    return o;
}

cJSON *control_json(const ControlSettings &s, const ControlStatus &st)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddBoolToObject(o, "tcpEnabled", s.tcp_enabled);
    cJSON_AddNumberToObject(o, "tcpPort", s.tcp_port);
    cJSON_AddBoolToObject(o, "udpEnabled", s.udp_enabled);
    cJSON_AddBoolToObject(o, "udpControl", s.udp_control);
    cJSON_AddBoolToObject(o, "udpAnnounce", s.udp_announce);
    cJSON_AddNumberToObject(o, "udpPort", s.udp_port);
    cJSON *status = cJSON_AddObjectToObject(o, "status");
    cJSON_AddNumberToObject(status, "tcpClients", st.tcp_clients);
    cJSON_AddNumberToObject(status, "tcpCommands", st.tcp_commands);
    cJSON_AddNumberToObject(status, "tcpAuthFailures", st.tcp_auth_failures);
    cJSON_AddNumberToObject(status, "udpCommands", st.udp_commands);
    cJSON_AddNumberToObject(status, "udpRejected", st.udp_rejected);
    cJSON_AddNumberToObject(status, "discoveryReplies", st.discovery_replies);
    return o;
}

cJSON *all_json(AppContext &ctx)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddItemToObject(o, "mqtt", mqtt_json(ctx.mqtt->settings(), ctx.mqtt->status()));
    cJSON_AddItemToObject(o, "control", control_json(ctx.control->settings(), ctx.control->status()));
    cJSON_AddStringToObject(o, "baseTopic", ctx.mqtt->base().c_str());
    return o;
}

} // namespace

void register_integration_routes(AppContext &ctx)
{
    WebServer &web = *ctx.web;

    web.route(HTTP_GET, "/integrations", RouteAuth::kUser, [&ctx](ApiRequest &req) { return send_data(req, all_json(ctx)); });

    web.route(HTTP_PUT, "/integrations/mqtt", RouteAuth::kUser, [&ctx](ApiRequest &req) {
        cJSON *b = req.json();
        if (!cJSON_IsObject(b)) return send_error(req, 400, "INVALID_REQUEST", "JSON object expected");
        MqttSettings s = ctx.mqtt->settings();
        double v = 0;
        api::get_bool(b, "enabled", s.enabled);
        api::get_string(b, "host", s.host, 128);
        if (api::get_number(b, "port", v)) {
            if (v < 1 || v > 65535) return send_error(req, 400, "INVALID_REQUEST", "port 1-65535");
            s.port = static_cast<uint16_t>(v);
        }
        api::get_bool(b, "tls", s.tls);
        api::get_string(b, "username", s.username, 64);
        api::get_string(b, "clientId", s.client_id, 64);
        api::get_string(b, "topicPrefix", s.topic_prefix, 33);
        api::get_string(b, "controllerId", s.controller_id, 33);
        if (api::get_number(b, "qos", v)) s.qos = static_cast<int>(v);
        if (api::get_number(b, "keepalive", v)) s.keepalive = static_cast<int>(v);
        api::get_bool(b, "retain", s.retain);
        std::string pw;
        const bool has_pw = api::get_string(b, "password", pw, 129);
        std::string error;
        if (ctx.mqtt->update(s, has_pw ? &pw : nullptr, error) != ESP_OK && !error.empty()) {
            return send_error(req, 400, "INVALID_CONFIGURATION", error);
        }
        return send_data(req, all_json(ctx));
    }, 4096);

    web.route(HTTP_PUT, "/integrations/homeassistant", RouteAuth::kUser, [&ctx](ApiRequest &req) {
        MqttSettings s = ctx.mqtt->settings();
        api::get_bool(req.json(), "enabled", s.ha_discovery);
        api::get_string(req.json(), "prefix", s.ha_prefix, 33);
        std::string error;
        if (ctx.mqtt->update(s, nullptr, error) != ESP_OK && !error.empty()) {
            return send_error(req, 400, "INVALID_CONFIGURATION", error);
        }
        return send_data(req, all_json(ctx));
    });

    web.route(HTTP_POST, "/integrations/mqtt/test", RouteAuth::kUser, [&ctx](ApiRequest &req) {
        const MqttStatus st = ctx.mqtt->status();
        cJSON *o = cJSON_CreateObject();
        cJSON_AddBoolToObject(o, "connected", st.connected);
        cJSON_AddStringToObject(o, "message", st.connected ? "Connected to the broker"
                                              : !st.enabled ? "MQTT is disabled"
                                              : st.last_error.empty() ? "Connecting…" : st.last_error.c_str());
        if (st.connected) {
            ctx.mqtt->event("{\"type\":\"test\",\"message\":\"Lumen MQTT test\"}");
        }
        return send_data(req, o);
    });

    auto control_update = [&ctx](ApiRequest &req) {
        cJSON *b = req.json();
        ControlSettings s = ctx.control->settings();
        double v = 0;
        api::get_bool(b, "tcpEnabled", s.tcp_enabled);
        if (api::get_number(b, "tcpPort", v)) s.tcp_port = static_cast<uint16_t>(v);
        api::get_bool(b, "udpEnabled", s.udp_enabled);
        api::get_bool(b, "udpControl", s.udp_control);
        api::get_bool(b, "udpAnnounce", s.udp_announce);
        if (api::get_number(b, "udpPort", v)) s.udp_port = static_cast<uint16_t>(v);
        std::string error;
        if (ctx.control->update(s, error) != ESP_OK) return send_error(req, 400, "INVALID_CONFIGURATION", error);
        return send_data(req, all_json(ctx));
    };
    web.route(HTTP_PUT, "/integrations/tcp", RouteAuth::kUser, control_update);
    web.route(HTTP_PUT, "/integrations/udp", RouteAuth::kUser, control_update);

    web.route(HTTP_GET, "/integrations/token", RouteAuth::kUser, [&ctx](ApiRequest &req) {
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "token", ctx.control->token().c_str());
        return send_data(req, o);
    });

    web.route(HTTP_POST, "/integrations/token/regenerate", RouteAuth::kUser, [&ctx](ApiRequest &req) {
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "token", ctx.control->regenerate_token().c_str());
        return send_data(req, o);
    });
}

} // namespace lumen
