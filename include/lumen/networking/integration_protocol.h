// Lumen — integration protocol helpers (SPEC §13–14). Pure C++ (host-tested).
//
// * Home Assistant "json" light schema <-> LightCommand / LightState
//   (MQTT set/state topics and the TCP protocol share it).
// * HA MQTT discovery documents per light type with stable unique ids.
// * TCP framing: newline-delimited JSON with a bounded line buffer (a TCP
//   read never has to contain exactly one message).
// * UDP authenticated control frames: HMAC-SHA256 over the payload with a
//   timestamp window and per-sender replay protection.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "core/cJSON.h"
#include "lumen/lighting/channel.h"
#include "lumen/lighting/transition.h"

namespace lumen
{
namespace integration
{

// HA JSON schema command -> LightCommand (partial update). Accepts:
//   {"state":"ON"|"OFF"|"TOGGLE", "brightness":0-100 (brightness_scale 100),
//    "color":{"r","g","b"}, "white":0-255, "color_temp_kelvin":K,
//    "color_temp":mireds, "transition":seconds, "effect":name}
// `current` resolves TOGGLE. Returns false with `error` on invalid input.
bool command_from_ha(const cJSON *obj, const LightState &current, LightCommand &cmd, std::string &error);

// LightState -> HA JSON state document.
cJSON *state_to_ha(const Channel &ch, const std::string &effect);

struct DiscoveryContext
{
    std::string discovery_prefix = "homeassistant";
    std::string topic_prefix = "lighting";
    std::string controller_id;   // e.g. "lumen-0798"
    std::string device_name;     // shown in HA
    std::string model;           // board profile name
    std::string sw_version;
    std::string configuration_url;
};

// Discovery topic + config for one light (HA "light" platform, json schema).
std::string discovery_topic(const DiscoveryContext &ctx, const std::string &light_id);
cJSON *discovery_config(const DiscoveryContext &ctx, const Channel &ch, const std::vector<std::string> &effects);

// Sanitise an id for MQTT topic levels / HA object ids ([a-z0-9_-]).
std::string topic_safe(const std::string &s);

// --- TCP line framing -------------------------------------------------------
class LineFramer
{
  public:
    explicit LineFramer(std::size_t max_line = 1024) : max_line_(max_line) {}
    // Appends received bytes; complete lines are appended to `out`. Returns
    // false when a line exceeded the limit (caller should drop the client).
    bool feed(const char *data, std::size_t len, std::vector<std::string> &out);
    void reset() { buf_.clear(); }

  private:
    std::size_t max_line_;
    std::string buf_;
};

// --- UDP authenticated frames ----------------------------------------------
// Frame: {"v":1,"ts":<unix s>,"seq":<u32>,"src":"<sender id>","cmd":{...},
//         "mac":"<hex hmac-sha256 of the canonical string>"}
// canonical = v + "|" + ts + "|" + seq + "|" + src + "|" + compact(cmd)
std::string canonical_frame(int v, int64_t ts, uint32_t seq, const std::string &src, const cJSON *cmd);

// HMAC-SHA256 (portable implementation; no crypto library dependency).
void hmac_sha256(const uint8_t *key, std::size_t key_len, const uint8_t *msg, std::size_t msg_len, uint8_t out[32]);
std::string hex(const uint8_t *data, std::size_t len);

class ReplayGuard
{
  public:
    static constexpr std::size_t kSenders = 8;
    static constexpr int64_t kWindowS = 30;
    // True when (src, seq, ts) is fresh: within ±30 s and seq above the
    // last accepted seq for that sender. Records it when accepted.
    bool accept(const std::string &src, uint32_t seq, int64_t ts, int64_t now);

  private:
    struct Slot
    {
        std::string src;
        uint32_t seq = 0;
        int64_t last_seen = 0;
    };
    Slot slots_[kSenders];
};

} // namespace integration
} // namespace lumen
