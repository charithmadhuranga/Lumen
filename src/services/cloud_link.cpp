// Lumen — cloud link logic. See cloud_link.hpp.

#include "lumen/services/cloud_link.hpp"

#include <cstdio>
#include <cstring>

#include "core/cJSON.h"
#include "lumen/core/crypto.hpp"

namespace lumen::cloud
{
namespace
{

bool b64url_char(char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
}

const char *str(const cJSON *o, const char *key)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, key);
    return cJSON_IsString(v) ? v->valuestring : nullptr;
}

bool uint32_of(const cJSON *o, const char *key, uint32_t &out)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, key);
    if (!cJSON_IsNumber(v) || v->valuedouble < 0 || v->valuedouble > 4294967295.0 ||
        static_cast<double>(static_cast<uint32_t>(v->valuedouble)) != v->valuedouble) {
        return false;
    }
    out = static_cast<uint32_t>(v->valuedouble);
    return true;
}

bool parse_ipv4(const std::string &s, uint8_t (&ip)[4])
{
    int parts = 0;
    std::size_t i = 0;
    while (parts < 4) {
        if (i >= s.size() || s[i] < '0' || s[i] > '9') return false;
        int v = 0, digits = 0;
        while (i < s.size() && s[i] >= '0' && s[i] <= '9') {
            v = v * 10 + (s[i] - '0');
            ++i;
            if (++digits > 3 || v > 255) return false;
        }
        if (digits > 1 && s[i - digits] == '0') return false; // no leading zeros (no octal confusion)
        ip[parts++] = static_cast<uint8_t>(v);
        if (parts < 4) {
            if (i >= s.size() || s[i] != '.') return false;
            ++i;
        }
    }
    return i == s.size();
}

} // namespace

bool is_uuid(const std::string &s)
{
    if (s.size() != 36) return false;
    for (std::size_t i = 0; i < 36; ++i) {
        const char c = s[i];
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            if (c != '-') return false;
        } else if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) {
            return false;
        }
    }
    return s[14] >= '1' && s[14] <= '8' && std::strchr("89ab", s[19]) != nullptr;
}

bool is_claim_token(const std::string &s)
{
    if (s.size() != 47 || s.compare(0, 4, "lct_") != 0) return false;
    for (std::size_t i = 4; i < s.size(); ++i) {
        if (!b64url_char(s[i])) return false;
    }
    return true;
}

bool is_lan_gateway_url(const std::string &url)
{
    if (url.size() > 32 || url.compare(0, 7, "http://") != 0) return false;
    std::string host = url.substr(7);
    const std::size_t colon = host.find(':');
    if (colon != std::string::npos) {
        const std::string port = host.substr(colon + 1);
        if (port.empty() || port.size() > 5) return false;
        long p = 0;
        for (char c : port) {
            if (c < '0' || c > '9') return false;
            p = p * 10 + (c - '0');
        }
        if (p < 1 || p > 65535) return false;
        host = host.substr(0, colon);
    }
    uint8_t ip[4];
    if (!parse_ipv4(host, ip)) return false;
    return ip[0] == 10 || (ip[0] == 172 && ip[1] >= 16 && ip[1] <= 31) || (ip[0] == 192 && ip[1] == 168) ||
           (ip[0] == 169 && ip[1] == 254);
}

bool parse_settings(const std::string &json, Settings &out, std::string &error)
{
    out = Settings{};
    cJSON *o = cJSON_ParseWithLength(json.data(), json.size());
    if (!cJSON_IsObject(o)) {
        cJSON_Delete(o);
        error = "not a JSON object";
        return false;
    }
    bool ok = true;
    const cJSON *ver = cJSON_GetObjectItemCaseSensitive(o, "schema_version");
    if (!cJSON_IsNumber(ver) || ver->valueint != kSchemaVersion) {
        error = "unsupported schemaVersion";
        ok = false;
    }
    const char *mode = str(o, "mode");
    if (ok && (mode == nullptr || (std::strcmp(mode, "off") != 0 && std::strcmp(mode, "gateway") != 0))) {
        error = "mode must be off or gateway";
        ok = false;
    }
    Settings s;
    if (ok && std::strcmp(mode, "gateway") == 0) {
        const char *dev = str(o, "deviceId"), *gw = str(o, "gatewayId"), *url = str(o, "gatewayUrl");
        if (dev == nullptr || gw == nullptr || url == nullptr || !is_uuid(dev) || !is_uuid(gw) || !is_lan_gateway_url(url) ||
            !uint32_of(o, "ticketEpoch", s.ticket_epoch)) {
            error = "paired settings are incomplete or invalid";
            ok = false;
        } else {
            s.paired = true;
            s.device_id = dev;
            s.gateway_id = gw;
            s.gateway_url = url;
            const cJSON *at = cJSON_GetObjectItemCaseSensitive(o, "pairedAt");
            s.paired_at = cJSON_IsNumber(at) && at->valuedouble > 0 ? static_cast<int64_t>(at->valuedouble) : 0;
        }
    }
    cJSON_Delete(o);
    if (ok) out = s;
    return ok;
}

std::string to_json(const Settings &s)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddNumberToObject(o, "schema_version", kSchemaVersion);
    cJSON_AddStringToObject(o, "mode", s.paired ? "gateway" : "off");
    if (s.paired) {
        cJSON_AddStringToObject(o, "deviceId", s.device_id.c_str());
        cJSON_AddStringToObject(o, "gatewayId", s.gateway_id.c_str());
        cJSON_AddStringToObject(o, "gatewayUrl", s.gateway_url.c_str());
        cJSON_AddNumberToObject(o, "ticketEpoch", s.ticket_epoch);
        cJSON_AddNumberToObject(o, "pairedAt", static_cast<double>(s.paired_at));
    }
    char *text = cJSON_PrintUnformatted(o);
    std::string out = text ? text : "{}";
    cJSON_free(text);
    cJSON_Delete(o);
    return out;
}

std::string register_request(const std::string &claim_token, const Hardware &hw, const std::string &fw,
                             const std::string &controller_id, const std::string &own_url)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddNumberToObject(o, "v", 1);
    cJSON_AddStringToObject(o, "claimToken", claim_token.c_str());
    cJSON *h = cJSON_AddObjectToObject(o, "hw");
    cJSON_AddStringToObject(h, "model", hw.model.c_str());
    if (!hw.board.empty()) cJSON_AddStringToObject(h, "board", hw.board.c_str());
    cJSON_AddStringToObject(h, "mac", hw.mac.c_str());
    if (!hw.chip.empty()) cJSON_AddStringToObject(h, "chip", hw.chip.c_str());
    cJSON_AddStringToObject(o, "fw", fw.c_str());
    cJSON_AddStringToObject(o, "controllerId", controller_id.c_str());
    cJSON_AddStringToObject(o, "url", own_url.c_str());
    char *text = cJSON_PrintUnformatted(o);
    std::string out = text ? text : "{}";
    cJSON_free(text);
    cJSON_Delete(o);
    return out;
}

bool parse_register_response(const std::string &body, Registration &out, std::string &error)
{
    out = Registration{};
    cJSON *o = cJSON_ParseWithLength(body.data(), body.size());
    if (!cJSON_IsObject(o)) {
        cJSON_Delete(o);
        error = "the gateway's reply is not JSON";
        return false;
    }
    bool ok = false;
    const cJSON *success = cJSON_GetObjectItemCaseSensitive(o, "success");
    const cJSON *data = cJSON_GetObjectItemCaseSensitive(o, "data");
    if (!cJSON_IsTrue(success)) {
        const cJSON *err = cJSON_GetObjectItemCaseSensitive(o, "error");
        const char *msg = cJSON_IsObject(err) ? str(err, "message") : nullptr;
        error = std::string("the gateway refused pairing: ") + (msg ? std::string(msg).substr(0, 120) : "no reason given");
    } else if (!cJSON_IsObject(data)) {
        error = "the gateway's reply has no data";
    } else {
        const cJSON *v = cJSON_GetObjectItemCaseSensitive(data, "v");
        const char *dev = str(data, "deviceId"), *gw = str(data, "gatewayId"), *key = str(data, "localKey");
        std::string raw;
        if (!cJSON_IsNumber(v) || v->valueint != 1) {
            error = "unsupported protocol version";
        } else if (dev == nullptr || gw == nullptr || !is_uuid(dev) || !is_uuid(gw)) {
            error = "invalid device or gateway id";
        } else if (key == nullptr || std::strlen(key) != 43 || !crypto::b64url_decode(key, raw) || raw.size() != 32) {
            error = "invalid local key";
        } else if (!uint32_of(data, "ticketEpoch", out.ticket_epoch)) {
            error = "invalid ticket epoch";
        } else {
            out.device_id = dev;
            out.gateway_id = gw;
            std::memcpy(out.local_key.data(), raw.data(), 32);
            ok = true;
        }
        if (!raw.empty()) std::memset(&raw[0], 0, raw.size());
    }
    cJSON_Delete(o);
    if (!ok) out = Registration{};
    return ok;
}

int64_t ticket_clock(int64_t wall_now, bool wall_synced, int64_t last_known)
{
    if (wall_synced) return wall_now > last_known ? wall_now : last_known;
    return last_known;
}

} // namespace lumen::cloud
