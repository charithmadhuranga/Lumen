// Lumen — local ticket verification. See ticket.hpp and docs/pro/PROTOCOL.md §6.

#include "lumen/services/ticket.hpp"

#include <cmath>
#include <cstring>

#include "core/cJSON.h"
#include "lumen/core/crypto.hpp"

namespace lumen::ticket
{
namespace
{

bool is_uuid(const char *s)
{
    // 8-4-4-4-12 lower-case hex, version 1-8, RFC 4122 variant.
    if (s == nullptr || std::strlen(s) != 36) return false;
    for (int i = 0; i < 36; ++i) {
        const char c = s[i];
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            if (c != '-') return false;
        } else if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) {
            return false;
        }
    }
    return s[14] >= '1' && s[14] <= '8' && (s[19] == '8' || s[19] == '9' || s[19] == 'a' || s[19] == 'b');
}

// JSON numbers outside strings must be plain integers: no fraction, exponent or sign
// (the reference implementation rejects floats; cJSON would turn 1e3 into 1000).
bool integers_only(const std::string &json)
{
    bool in_string = false, escaped = false;
    for (char c : json) {
        if (in_string) {
            if (escaped) {
                escaped = false;
            } else if (c == '\\') {
                escaped = true;
            } else if (c == '"') {
                in_string = false;
            }
            continue;
        }
        if (c == '"') {
            in_string = true;
        } else if (c == '.' || c == 'e' || c == 'E' || c == '+' || c == '-') {
            return false;
        }
    }
    return !in_string;
}

bool get_int(const cJSON *o, const char *key, int64_t max, int64_t &out)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, key);
    if (!cJSON_IsNumber(v)) return false;
    const double d = v->valuedouble;
    if (d < 0 || d > static_cast<double>(max) || std::floor(d) != d) return false;
    out = static_cast<int64_t>(d);
    return true;
}

bool parse_claims(const std::string &json, Claims &c)
{
    if (!integers_only(json)) return false;
    cJSON *o = cJSON_ParseWithLength(json.data(), json.size());
    if (o == nullptr) return false;
    bool ok = cJSON_IsObject(o) && cJSON_GetArraySize(o) == 6;
    // Exactly the six keys, each once: cJSON keeps duplicate keys as separate items, so a
    // count of six plus all six present means no key is missing or repeated.
    const char *keys[] = {"sub", "dev", "scp", "iat", "exp", "ep"};
    for (const char *k : keys) ok = ok && cJSON_GetObjectItemCaseSensitive(o, k) != nullptr;
    const cJSON *sub = cJSON_GetObjectItemCaseSensitive(o, "sub");
    const cJSON *dev = cJSON_GetObjectItemCaseSensitive(o, "dev");
    const cJSON *scp = cJSON_GetObjectItemCaseSensitive(o, "scp");
    ok = ok && cJSON_IsString(sub) && cJSON_IsString(dev) && cJSON_IsString(scp);
    if (ok) {
        const char *s = sub->valuestring;
        ok = std::strlen(s) == 38 && (s[0] == 'u' || s[0] == 'g') && s[1] == ':' && is_uuid(s + 2) &&
             is_uuid(dev->valuestring);
        if (std::strcmp(scp->valuestring, "control") == 0) {
            c.scope = Scope::kControl;
        } else if (std::strcmp(scp->valuestring, "admin") == 0) {
            c.scope = Scope::kAdmin;
        } else {
            ok = false;
        }
    }
    int64_t ep = 0;
    ok = ok && get_int(o, "iat", 4102444800LL, c.iat) && get_int(o, "exp", 4102444800LL, c.exp) &&
         get_int(o, "ep", 4294967295LL, ep);
    if (ok) {
        c.sub = sub->valuestring;
        c.dev = dev->valuestring;
        c.ep = static_cast<uint32_t>(ep);
    }
    cJSON_Delete(o);
    return ok;
}

} // namespace

const char *reason_name(Reason r)
{
    switch (r) {
    case Reason::kOk: return "ok";
    case Reason::kFormat: return "format";
    case Reason::kMac: return "mac";
    case Reason::kClaims: return "claims";
    case Reason::kDevice: return "device";
    case Reason::kEpoch: return "epoch";
    case Reason::kLifetime: return "lifetime";
    case Reason::kNotYetValid: return "not_yet_valid";
    case Reason::kExpired: return "expired";
    }
    return "format";
}

Result verify(const std::string &ticket, const uint8_t *key, const std::string &device, uint32_t epoch, int64_t now)
{
    Result r;
    if (ticket.size() > kMaxLength || ticket.compare(0, 4, "LT1.") != 0) return r;
    const std::size_t dot = ticket.find('.', 4);
    if (dot == std::string::npos || ticket.find('.', dot + 1) != std::string::npos) return r;
    const std::string claims_b64 = ticket.substr(4, dot - 4);
    const std::string mac_b64 = ticket.substr(dot + 1);
    std::string claims_json, mac;
    if (claims_b64.empty() || mac_b64.empty() || !crypto::b64url_decode(claims_b64, claims_json) ||
        !crypto::b64url_decode(mac_b64, mac)) {
        return r;
    }
    const crypto::Digest expected = crypto::hmac_sha256(key, kKeyLength, reinterpret_cast<const uint8_t *>(ticket.data()), dot);
    if (mac.size() != expected.size() ||
        !crypto::equal_ct(reinterpret_cast<const uint8_t *>(mac.data()), expected.data(), expected.size())) {
        r.reason = Reason::kMac;
        return r;
    }
    Claims c;
    if (!parse_claims(claims_json, c)) {
        r.reason = Reason::kClaims;
        return r;
    }
    if (c.dev != device) {
        r.reason = Reason::kDevice;
    } else if (c.ep != epoch) {
        r.reason = Reason::kEpoch;
    } else if (c.exp <= c.iat || c.exp - c.iat > (c.scope == Scope::kAdmin ? kMaxAdminLifetime : kMaxControlLifetime)) {
        r.reason = Reason::kLifetime;
    } else if (c.iat > now + kClockSkew) {
        r.reason = Reason::kNotYetValid;
    } else if (c.exp <= now) {
        r.reason = Reason::kExpired;
    } else {
        r.reason = Reason::kOk;
        r.claims = c;
    }
    return r;
}

} // namespace lumen::ticket
