// Lumen — integration protocol helpers. See integration_protocol.h
#include "lumen/networking/integration_protocol.h"
#include "lumen/lighting/lighting_task.h"

#include <cmath>
#include <cstring>

namespace lumen
{
namespace integration
{

namespace
{

bool num(const cJSON *o, const char *k, double &v)
{
    const cJSON *it = cJSON_GetObjectItemCaseSensitive(o, k);
    if (!cJSON_IsNumber(it)) return false;
    v = it->valuedouble;
    return true;
}

bool is_rgb(LightType t)
{
    return t == LightType::kRgb || t == LightType::kRgbw || t == LightType::kAddressableRgb ||
           t == LightType::kAddressableRgbw;
}

// --- SHA-256 (FIPS 180-4), compact portable implementation ---------------
struct Sha256
{
    uint32_t h[8];
    uint8_t block[64];
    std::size_t used = 0;
    uint64_t bits = 0;

    Sha256()
    {
        static const uint32_t init[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                                         0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
        std::memcpy(h, init, sizeof(h));
    }
    static uint32_t rotr(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }
    void compress()
    {
        static const uint32_t k[64] = {
            0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
            0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
            0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
            0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
            0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
            0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
            0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
            0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
        uint32_t w[64];
        for (int i = 0; i < 16; ++i) {
            w[i] = (static_cast<uint32_t>(block[i * 4]) << 24) | (static_cast<uint32_t>(block[i * 4 + 1]) << 16) |
                   (static_cast<uint32_t>(block[i * 4 + 2]) << 8) | block[i * 4 + 3];
        }
        for (int i = 16; i < 64; ++i) {
            const uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            const uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
        for (int i = 0; i < 64; ++i) {
            const uint32_t s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
            const uint32_t ch = (e & f) ^ (~e & g);
            const uint32_t t1 = hh + s1 + ch + k[i] + w[i];
            const uint32_t s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
            const uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            const uint32_t t2 = s0 + maj;
            hh = g;
            g = f;
            f = e;
            e = d + t1;
            d = c;
            c = b;
            b = a;
            a = t1 + t2;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
    }
    void update(const uint8_t *data, std::size_t len)
    {
        bits += static_cast<uint64_t>(len) * 8;
        while (len > 0) {
            const std::size_t n = std::min(len, 64 - used);
            std::memcpy(block + used, data, n);
            used += n;
            data += n;
            len -= n;
            if (used == 64) {
                compress();
                used = 0;
            }
        }
    }
    void finish(uint8_t out[32])
    {
        const uint64_t total = bits;
        const uint8_t pad = 0x80;
        update(&pad, 1);
        const uint8_t zero = 0;
        while (used != 56) update(&zero, 1);
        uint8_t len[8];
        for (int i = 0; i < 8; ++i) len[i] = static_cast<uint8_t>(total >> (56 - 8 * i));
        update(len, 8);
        for (int i = 0; i < 8; ++i) {
            out[i * 4] = static_cast<uint8_t>(h[i] >> 24);
            out[i * 4 + 1] = static_cast<uint8_t>(h[i] >> 16);
            out[i * 4 + 2] = static_cast<uint8_t>(h[i] >> 8);
            out[i * 4 + 3] = static_cast<uint8_t>(h[i]);
        }
    }
};

} // namespace

bool command_from_ha(const cJSON *obj, const LightState &current, LightCommand &cmd, std::string &error)
{
    if (!cJSON_IsObject(obj)) {
        error = "payload must be a JSON object";
        return false;
    }
    cmd.fields = 0;
    const cJSON *state = cJSON_GetObjectItemCaseSensitive(obj, "state");
    if (state != nullptr) {
        if (!cJSON_IsString(state)) {
            error = "state must be ON, OFF or TOGGLE";
            return false;
        }
        const std::string s = state->valuestring;
        if (s == "ON") cmd.state.power = true;
        else if (s == "OFF") cmd.state.power = false;
        else if (s == "TOGGLE") cmd.state.power = !current.power;
        else {
            error = "state must be ON, OFF or TOGGLE";
            return false;
        }
        cmd.fields |= kFieldPower;
    }
    double v = 0;
    if (num(obj, "brightness", v)) {
        if (v < 0 || v > 100) {
            error = "brightness must be 0-100";
            return false;
        }
        cmd.state.brightness = static_cast<uint8_t>(std::lround(v));
        cmd.fields |= kFieldBrightness;
    }
    const cJSON *color = cJSON_GetObjectItemCaseSensitive(obj, "color");
    if (cJSON_IsObject(color)) {
        double r = 0, g = 0, b = 0;
        if (!num(color, "r", r) || !num(color, "g", g) || !num(color, "b", b) || r < 0 || g < 0 || b < 0 ||
            r > 255 || g > 255 || b > 255) {
            error = "color must be {r,g,b} 0-255";
            return false;
        }
        cmd.state.red = static_cast<uint8_t>(r);
        cmd.state.green = static_cast<uint8_t>(g);
        cmd.state.blue = static_cast<uint8_t>(b);
        cmd.fields |= kFieldColor;
        if (num(color, "w", v) && v >= 0 && v <= 255) {
            cmd.state.white = static_cast<uint8_t>(v);
            cmd.fields |= kFieldWhite;
        }
    }
    if (num(obj, "white", v)) {
        if (v < 0 || v > 255) {
            error = "white must be 0-255";
            return false;
        }
        cmd.state.white = static_cast<uint8_t>(v);
        cmd.fields |= kFieldWhite;
    }
    // Our discovery sets color_temp_kelvin:true, so Home Assistant sends
    // "color_temp" in kelvin. Values below 1000 can only be mireds (other
    // clients / legacy HA), which keeps both forms unambiguous.
    if (num(obj, "color_temp_kelvin", v) || num(obj, "color_temp", v)) {
        if (v > 0 && v < 1000) v = 1000000.0 / v;
        if (v < 1000 || v > 20000) {
            error = "colour temperature out of range";
            return false;
        }
        cmd.state.color_temperature = static_cast<uint16_t>(std::lround(v));
        cmd.fields |= kFieldCct;
    }
    if (num(obj, "transition", v)) {
        if (v < 0 || v > 600) {
            error = "transition must be 0-600 s";
            return false;
        }
        cmd.transition_ms = static_cast<uint32_t>(v * 1000.0);
        cmd.use_default_transition = false;
    } else {
        cmd.use_default_transition = true;
    }
    const cJSON *effect = cJSON_GetObjectItemCaseSensitive(obj, "effect");
    if (cJSON_IsString(effect)) {
        cmd.effect = effect->valuestring;
        if (cmd.effect == "none") cmd.effect = "stop";
        EffectType et{};
        if (cmd.effect != "stop" && !effect_type_from_string(cmd.effect, et)) {
            error = "unknown effect '" + cmd.effect.substr(0, 32) + "'";
            return false;
        }
    }
    if (cmd.fields == 0 && cmd.effect.empty()) {
        error = "nothing to change";
        return false;
    }
    return true;
}

cJSON *state_to_ha(const Channel &ch, const std::string &effect)
{
    const LightState &s = ch.requested;
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "state", s.power ? "ON" : "OFF");
    if (ch.type != LightType::kSwitch) {
        cJSON_AddNumberToObject(o, "brightness", s.brightness);
    }
    if (is_rgb(ch.type)) {
        cJSON_AddStringToObject(o, "color_mode", ch.type == LightType::kRgbw || ch.type == LightType::kAddressableRgbw ? "rgbw" : "rgb");
        cJSON *c = cJSON_AddObjectToObject(o, "color");
        cJSON_AddNumberToObject(c, "r", s.red);
        cJSON_AddNumberToObject(c, "g", s.green);
        cJSON_AddNumberToObject(c, "b", s.blue);
        if (ch.type == LightType::kRgbw || ch.type == LightType::kAddressableRgbw) cJSON_AddNumberToObject(c, "w", s.white);
    } else if (ch.type == LightType::kCct) {
        cJSON_AddStringToObject(o, "color_mode", "color_temp");
        // HA JSON schema with color_temp_kelvin:true reads "color_temp" (K).
        cJSON_AddNumberToObject(o, "color_temp", s.color_temperature);
    } else if (ch.type == LightType::kDimmer) {
        cJSON_AddStringToObject(o, "color_mode", "brightness");
    } else {
        cJSON_AddStringToObject(o, "color_mode", "onoff");
    }
    if (ch.type != LightType::kSwitch) {
        cJSON_AddStringToObject(o, "effect", effect.empty() ? "none" : effect.c_str());
    }
    return o;
}

std::string topic_safe(const std::string &s)
{
    std::string out;
    for (char c : s) {
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-') out.push_back(c);
        else if (c >= 'A' && c <= 'Z') out.push_back(static_cast<char>(c - 'A' + 'a'));
        else out.push_back('_');
    }
    return out;
}

std::string entity_slug(const std::string &s)
{
    std::string out;
    for (char c : s) {
        const bool alnum = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
        if (c >= 'A' && c <= 'Z') out.push_back(static_cast<char>(c - 'A' + 'a'));
        else if (alnum) out.push_back(c);
        else if (!out.empty() && out.back() != '_') out.push_back('_');
    }
    while (!out.empty() && out.back() == '_') out.pop_back();
    return out.empty() ? "lumen" : out;
}

std::string device_display_name(const std::string &controller_id)
{
    // "lumen-0798" -> "Lumen 0798" (HA prefixes entity names with this).
    std::string rest = controller_id;
    if (rest.rfind("lumen-", 0) == 0 || rest.rfind("lumen_", 0) == 0) rest = rest.substr(6);
    return rest.empty() ? "Lumen" : "Lumen " + rest;
}

std::string discovery_topic(const DiscoveryContext &ctx, const std::string &light_id)
{
    return ctx.discovery_prefix + "/light/" + topic_safe(ctx.controller_id) + "/" + topic_safe(light_id) + "/config";
}

cJSON *discovery_config(const DiscoveryContext &ctx, const Channel &ch, const std::vector<std::string> &effects)
{
    const std::string base = ctx.topic_prefix + "/" + ctx.controller_id;
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "name", ch.name.c_str());
    cJSON_AddStringToObject(o, "unique_id", (topic_safe(ctx.controller_id) + "_" + topic_safe(ch.id)).c_str());
    // object_id is ignored by current Home Assistant; default_entity_id sets
    // the entity id on first registration (users may rename it later).
    cJSON_AddStringToObject(o, "default_entity_id", ("light." + entity_slug(ctx.controller_id + "_" + ch.id)).c_str());
    cJSON_AddStringToObject(o, "schema", "json");
    cJSON_AddStringToObject(o, "command_topic", (base + "/light/" + ch.id + "/set").c_str());
    cJSON_AddStringToObject(o, "state_topic", (base + "/light/" + ch.id + "/state").c_str());
    cJSON_AddStringToObject(o, "availability_topic", (base + "/status").c_str());
    cJSON *modes = cJSON_AddArrayToObject(o, "supported_color_modes");
    switch (ch.type) {
    case LightType::kSwitch: cJSON_AddItemToArray(modes, cJSON_CreateString("onoff")); break;
    case LightType::kDimmer: cJSON_AddItemToArray(modes, cJSON_CreateString("brightness")); break;
    case LightType::kRgb:
    case LightType::kAddressableRgb: cJSON_AddItemToArray(modes, cJSON_CreateString("rgb")); break;
    case LightType::kRgbw:
    case LightType::kAddressableRgbw: cJSON_AddItemToArray(modes, cJSON_CreateString("rgbw")); break;
    case LightType::kCct:
        cJSON_AddItemToArray(modes, cJSON_CreateString("color_temp"));
        cJSON_AddBoolToObject(o, "color_temp_kelvin", true);
        cJSON_AddNumberToObject(o, "min_kelvin", ch.limits.color_temperature_min);
        cJSON_AddNumberToObject(o, "max_kelvin", ch.limits.color_temperature_max);
        break;
    }
    if (ch.type != LightType::kSwitch) {
        cJSON_AddBoolToObject(o, "brightness", true);
        cJSON_AddNumberToObject(o, "brightness_scale", 100);
        cJSON_AddBoolToObject(o, "transition", true);
        if (!effects.empty()) {
            cJSON_AddBoolToObject(o, "effect", true);
            cJSON *list = cJSON_AddArrayToObject(o, "effect_list");
            for (const auto &e : effects) cJSON_AddItemToArray(list, cJSON_CreateString(e.c_str()));
        }
    }
    cJSON *dev = cJSON_AddObjectToObject(o, "device");
    cJSON *ids = cJSON_AddArrayToObject(dev, "identifiers");
    cJSON_AddItemToArray(ids, cJSON_CreateString(("lumen_" + topic_safe(ctx.controller_id)).c_str()));
    cJSON_AddStringToObject(dev, "name", ctx.device_name.c_str());
    cJSON_AddStringToObject(dev, "manufacturer", "Lumen");
    cJSON_AddStringToObject(dev, "model", ctx.model.c_str());
    cJSON_AddStringToObject(dev, "sw_version", ctx.sw_version.c_str());
    if (!ctx.configuration_url.empty()) cJSON_AddStringToObject(dev, "configuration_url", ctx.configuration_url.c_str());
    return o;
}

bool LineFramer::feed(const char *data, std::size_t len, std::vector<std::string> &out)
{
    for (std::size_t i = 0; i < len; ++i) {
        const char c = data[i];
        if (c == '\n') {
            if (!buf_.empty() && buf_.back() == '\r') buf_.pop_back();
            if (!buf_.empty()) out.push_back(buf_);
            buf_.clear();
        } else {
            if (buf_.size() >= max_line_) {
                buf_.clear();
                return false;
            }
            buf_.push_back(c);
        }
    }
    return true;
}

std::string canonical_frame(int v, int64_t ts, uint32_t seq, const std::string &src, const cJSON *cmd)
{
    char *text = cJSON_PrintUnformatted(cmd);
    std::string out = std::to_string(v) + "|" + std::to_string(ts) + "|" + std::to_string(seq) + "|" + src + "|" +
                      (text ? text : "");
    cJSON_free(text);
    return out;
}

void hmac_sha256(const uint8_t *key, std::size_t key_len, const uint8_t *msg, std::size_t msg_len, uint8_t out[32])
{
    uint8_t k[64] = {};
    if (key_len > 64) {
        Sha256 hk;
        hk.update(key, key_len);
        hk.finish(k);
    } else {
        std::memcpy(k, key, key_len);
    }
    uint8_t ipad[64], opad[64];
    for (int i = 0; i < 64; ++i) {
        ipad[i] = static_cast<uint8_t>(k[i] ^ 0x36);
        opad[i] = static_cast<uint8_t>(k[i] ^ 0x5c);
    }
    uint8_t inner[32];
    Sha256 hi;
    hi.update(ipad, 64);
    hi.update(msg, msg_len);
    hi.finish(inner);
    Sha256 ho;
    ho.update(opad, 64);
    ho.update(inner, 32);
    ho.finish(out);
}

std::string hex(const uint8_t *data, std::size_t len)
{
    static const char *digits = "0123456789abcdef";
    std::string out;
    out.reserve(len * 2);
    for (std::size_t i = 0; i < len; ++i) {
        out.push_back(digits[data[i] >> 4]);
        out.push_back(digits[data[i] & 0x0F]);
    }
    return out;
}

bool ReplayGuard::accept(const std::string &src, uint32_t seq, int64_t ts, int64_t now)
{
    if (src.empty() || ts < now - kWindowS || ts > now + kWindowS) return false;
    Slot *slot = nullptr;
    Slot *oldest = &slots_[0];
    for (auto &s : slots_) {
        if (s.src == src) {
            slot = &s;
            break;
        }
        if (s.last_seen < oldest->last_seen) oldest = &s;
    }
    if (slot != nullptr) {
        if (seq <= slot->seq) return false; // replayed / reordered
    } else {
        slot = oldest; // evict the least recently seen sender
        *slot = Slot{};
        slot->src = src;
    }
    slot->seq = seq;
    slot->last_seen = now;
    return true;
}

} // namespace integration
} // namespace lumen
