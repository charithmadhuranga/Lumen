#include "lumen/networking/espnow_protocol.h"

#include <cstring>

#include "lumen/networking/integration_protocol.h"

namespace lumen::espnow
{

namespace
{

void put16(uint8_t *p, uint16_t v)
{
    p[0] = static_cast<uint8_t>(v);
    p[1] = static_cast<uint8_t>(v >> 8);
}
void put32(uint8_t *p, uint32_t v)
{
    for (int i = 0; i < 4; ++i) p[i] = static_cast<uint8_t>(v >> (8 * i));
}
uint16_t get16(const uint8_t *p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
uint32_t get32(const uint8_t *p)
{
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

void mac_of(const std::string &key, const uint8_t *data, std::size_t len, uint8_t out[32])
{
    integration::hmac_sha256(reinterpret_cast<const uint8_t *>(key.data()), key.size(), data, len, out);
}

bool valid_type(uint8_t t)
{
    return t >= static_cast<uint8_t>(Type::kHello) && t <= static_cast<uint8_t>(Type::kScene);
}

} // namespace

uint32_t network_tag(const std::string &network_id)
{
    static const char kLabel[] = "lumen-espnow-network";
    uint8_t h[32];
    integration::hmac_sha256(reinterpret_cast<const uint8_t *>(kLabel), sizeof(kLabel) - 1,
                             reinterpret_cast<const uint8_t *>(network_id.data()), network_id.size(), h);
    return get32(h);
}

bool encode(const Frame &f, const std::string &key, std::vector<uint8_t> &out)
{
    if (f.payload.size() > kMaxPayload) return false;
    out.assign(kHeader + f.payload.size() + kMacLen, 0);
    uint8_t *p = out.data();
    p[0] = 'L';
    p[1] = 'N';
    p[2] = kVersion;
    p[3] = static_cast<uint8_t>(f.type);
    p[4] = f.flags;
    p[5] = 0;
    put32(p + 6, f.network);
    put32(p + 10, f.seq);
    put32(p + 14, f.msg_id);
    put32(p + 18, f.timestamp);
    put16(p + 22, static_cast<uint16_t>(f.payload.size()));
    std::memcpy(p + kHeader, f.payload.data(), f.payload.size());
    uint8_t mac[32];
    mac_of(key, p, kHeader + f.payload.size(), mac);
    std::memcpy(p + kHeader + f.payload.size(), mac, kMacLen);
    return true;
}

Reject decode(const uint8_t *data, std::size_t len, const std::string &key, uint32_t expect_network, Frame &out)
{
    if (data == nullptr || len < kHeader + kMacLen || len > kMaxFrame || data[0] != 'L' || data[1] != 'N') {
        return Reject::kMalformed;
    }
    if (data[2] != kVersion) return Reject::kVersion;
    const std::size_t plen = get16(data + 22);
    if (plen > kMaxPayload || kHeader + plen + kMacLen != len || !valid_type(data[3]) || data[5] != 0) {
        return Reject::kMalformed;
    }
    const uint32_t network = get32(data + 6);
    if (expect_network != 0 && network != expect_network) return Reject::kNetwork;
    uint8_t mac[32];
    mac_of(key, data, kHeader + plen, mac);
    uint8_t diff = 0; // constant time
    for (std::size_t i = 0; i < kMacLen; ++i) diff |= static_cast<uint8_t>(mac[i] ^ data[kHeader + plen + i]);
    if (diff != 0) return Reject::kAuth;
    out.type = static_cast<Type>(data[3]);
    out.flags = data[4];
    out.network = network;
    out.seq = get32(data + 10);
    out.msg_id = get32(data + 14);
    out.timestamp = get32(data + 18);
    out.payload.assign(reinterpret_cast<const char *>(data + kHeader), plen);
    return Reject::kNone;
}

const char *reject_name(Reject r)
{
    switch (r) {
    case Reject::kNone: return "ok";
    case Reject::kMalformed: return "malformed";
    case Reject::kVersion: return "version";
    case Reject::kNetwork: return "network";
    case Reject::kAuth: return "auth";
    }
    return "?";
}

ReplayFilter::Verdict ReplayFilter::check(const std::array<uint8_t, 6> &sender, const Frame &f, int64_t now_unix, bool now_valid)
{
    if ((f.flags & kFlagTsValid) && now_valid) {
        const int64_t d = static_cast<int64_t>(f.timestamp) - now_unix;
        if (d > kWindowS || d < -kWindowS) return Verdict::kStale;
    }
    for (uint32_t id : recent_) {
        if (id != 0 && id == f.msg_id) return Verdict::kDuplicate;
    }
    Sender *slot = nullptr;
    for (auto &s : senders_) {
        if (s.used && s.mac == sender) {
            slot = &s;
            break;
        }
    }
    if (slot == nullptr) {
        for (auto &s : senders_) {
            if (!s.used) {
                slot = &s;
                slot->used = true;
                slot->mac = sender;
                slot->last_seq = 0;
                break;
            }
        }
        if (slot == nullptr) return Verdict::kFull;
    } else if (f.seq <= slot->last_seq) {
        return Verdict::kReplay;
    }
    slot->last_seq = f.seq;
    recent_[recent_next_] = f.msg_id;
    recent_next_ = (recent_next_ + 1) % kRecentIds;
    return Verdict::kAccept;
}

} // namespace lumen::espnow
