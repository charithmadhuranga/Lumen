// Lumen — local ticket verification (docs/pro/PROTOCOL.md §6, Lumen Pro P5).
//
// A ticket "LT1.<b64url(claims)>.<b64url(HMAC-SHA256(K_local, "LT1." + b64url(claims)))>"
// lets a phone or the gateway use this controller's local API without the
// admin password. Checks run in the protocol's fixed order so the firmware,
// gateway, cloud and reference implementation report the same reason:
//   format -> mac -> claims -> device -> epoch -> lifetime -> not_yet_valid -> expired
// Pure function: no I/O, no clock, no globals (host-tested with the protocol vectors).
#pragma once

#include <cstdint>
#include <string>

namespace lumen::ticket
{

constexpr std::size_t kMaxLength = 512;
constexpr int64_t kMaxControlLifetime = 7 * 24 * 3600;
constexpr int64_t kMaxAdminLifetime = 12 * 3600;
constexpr int64_t kClockSkew = 300;
constexpr std::size_t kKeyLength = 32;

enum class Scope : uint8_t
{
    kControl,
    kAdmin,
};

enum class Reason : uint8_t
{
    kOk = 0,
    kFormat,
    kMac,
    kClaims,
    kDevice,
    kEpoch,
    kLifetime,
    kNotYetValid,
    kExpired,
};

const char *reason_name(Reason r); // protocol name, e.g. "not_yet_valid"

struct Claims
{
    std::string sub; // "u:<uuid>" (app user) or "g:<uuid>" (gateway)
    std::string dev;
    Scope scope = Scope::kControl;
    int64_t iat = 0;
    int64_t exp = 0;
    uint32_t ep = 0;
};

struct Result
{
    Reason reason = Reason::kFormat;
    Claims claims; // valid only when reason == kOk
    bool ok() const { return reason == Reason::kOk; }
};

// `key` must be kKeyLength bytes. `now` is Unix seconds: synchronised wall
// clock, or the persisted last known time when the clock is not set.
Result verify(const std::string &ticket, const uint8_t *key, const std::string &device, uint32_t epoch, int64_t now);

} // namespace lumen::ticket
