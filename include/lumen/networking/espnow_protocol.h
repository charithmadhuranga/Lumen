// Lumen — ESP-NOW multi-controller protocol v1 (SPEC §12, docs/ESP_NOW_PROTOCOL.md).
// Pure encode/decode/validation, host-tested; no ESP-IDF dependency.
//
// Frame (little-endian), ≤ 250 bytes (ESP-NOW v1 limit):
//   0  'L' 'N'          magic
//   2  version = 1
//   3  type             kHello | kState | kCommand | kScene
//   4  flags            bit0: timestamp valid (sender clock synchronised)
//   5  reserved = 0
//   6  network tag u32  first 4 bytes of SHA-256(network id)
//   10 seq u32          per-sender, strictly increasing (boot count << 20 | counter)
//   14 msg id u32       random; duplicate detection
//   18 timestamp u32    unix seconds (0 when not synchronised)
//   22 payload len u16  ≤ kMaxPayload
//   24 payload          compact JSON (≤ 210 bytes)
//   .. mac[16]          HMAC-SHA256(network key, bytes[0 .. 24+len)) truncated
// Link-layer encryption is not used; authentication is the HMAC above.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace lumen::espnow
{

constexpr uint8_t kVersion = 1;
constexpr std::size_t kHeader = 24;
constexpr std::size_t kMacLen = 16;
constexpr std::size_t kMaxFrame = 250;
constexpr std::size_t kMaxPayload = kMaxFrame - kHeader - kMacLen; // 210
constexpr uint8_t kFlagTsValid = 0x01;

enum class Type : uint8_t
{
    kHello = 1,   // heartbeat + identity (every 5 s)
    kState = 2,   // owner's light state (on change)
    kCommand = 3, // command for a light owned by the addressed controller
    kScene = 4,   // network-wide scene activation (by scene id)
};

struct Frame
{
    Type type = Type::kHello;
    uint8_t flags = 0;
    uint32_t network = 0;
    uint32_t seq = 0;
    uint32_t msg_id = 0;
    uint32_t timestamp = 0;
    std::string payload;
};

enum class Reject : uint8_t
{
    kNone = 0,
    kMalformed,  // size/magic/length/type
    kVersion,    // unknown protocol version
    kNetwork,    // other network id
    kAuth,       // bad HMAC (wrong key / tampered)
};

uint32_t network_tag(const std::string &network_id);

// Encodes into `out`; false when the payload is too large.
bool encode(const Frame &f, const std::string &key, std::vector<uint8_t> &out);
// Validates structure, network and HMAC. `expect_network` 0 = any.
Reject decode(const uint8_t *data, std::size_t len, const std::string &key, uint32_t expect_network, Frame &out);

const char *reject_name(Reject r);

// Per-sender replay/duplicate protection (bounded): a frame is accepted only
// if its seq is newer than the last accepted from that sender, its msg id has
// not been seen recently, and — when both clocks are valid — its timestamp is
// within ±kWindowS of ours.
class ReplayFilter
{
  public:
    static constexpr std::size_t kSenders = 16;
    static constexpr std::size_t kRecentIds = 32;
    static constexpr int64_t kWindowS = 120;

    enum class Verdict : uint8_t { kAccept, kReplay, kDuplicate, kStale, kFull };

    Verdict check(const std::array<uint8_t, 6> &sender, const Frame &f, int64_t now_unix, bool now_valid);

  private:
    struct Sender
    {
        bool used = false;
        std::array<uint8_t, 6> mac{};
        uint32_t last_seq = 0;
    };
    std::array<Sender, kSenders> senders_{};
    std::array<uint32_t, kRecentIds> recent_{};
    std::size_t recent_next_ = 0;
};

} // namespace lumen::espnow
