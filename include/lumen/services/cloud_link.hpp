// Lumen — Lumen Cloud link of a controller: settings, pairing messages, LAN URL rules.
//
// DECISIONS D-17: a controller never talks to the cloud. It is paired with a
// Lumen Gateway on the LAN, which represents it to the cloud and reaches it
// through the local API with tickets (PROTOCOL.md §6, §8, §9).
//
// Pure logic, host-tested: no I/O. The device side (NVS key store, HTTP call,
// routes) is in src/app/cloud_service.cpp and src/web/api_cloud.cpp.
#pragma once

#include <array>
#include <cstdint>
#include <string>

namespace lumen::cloud
{

constexpr const char *kDocument = "cloud.json"; // config/cloud.json, schemas/cloud.schema.json
constexpr int kSchemaVersion = 1;

// config/cloud.json. No secrets: K_local and the last known time are in NVS ("cloud").
struct Settings
{
    bool paired = false;         // mode "gateway" when true, "off" otherwise
    std::string device_id;       // cloud UUID of this controller
    std::string gateway_id;      // UUID of the gateway that represents it
    std::string gateway_url;     // http://<private IPv4>[:port]
    uint32_t ticket_epoch = 0;   // only increases (raising it revokes older tickets)
    int64_t paired_at = 0;       // Unix seconds (0 if the clock was not set)
};

// Parse/serialise config/cloud.json. parse() returns false (and leaves `out`
// unpaired) for anything invalid; a missing document means "off".
bool parse_settings(const std::string &json, Settings &out, std::string &error);
std::string to_json(const Settings &s);

bool is_uuid(const std::string &s);
bool is_claim_token(const std::string &s); // "lct_" + 43 base64url characters
// Gateway URL accepted for pairing: http://<IPv4 in 10/8, 172.16/12, 192.168/16
// or 169.254/16>[:port], nothing else (no path, no hostname, no https). Keeps
// the key exchange on the local network.
bool is_lan_gateway_url(const std::string &url);

struct Hardware
{
    std::string model;
    std::string board;
    std::string mac;  // aa:bb:cc:dd:ee:ff, lower case
    std::string chip;
};

// Body of POST {gatewayUrl}/local/v1/register (protocol register-request).
std::string register_request(const std::string &claim_token, const Hardware &hw, const std::string &fw,
                             const std::string &controller_id, const std::string &own_url);

struct Registration
{
    std::string device_id;
    std::string gateway_id;
    uint32_t ticket_epoch = 0;
    std::array<uint8_t, 32> local_key{};
};

// Parse the gateway's reply: {"success":true,"data":<register-response>}. On
// {"success":false,...} or anything malformed returns false with `error` set
// (the gateway's error message when it sent one; never echoes key material).
bool parse_register_response(const std::string &body, Registration &out, std::string &error);

// Clock used for ticket expiry: the wall clock when it is synchronised,
// otherwise the persisted last known time (a rebooted controller without
// internet still accepts unexpired tickets, and never accepts a ticket it
// already saw expire).
int64_t ticket_clock(int64_t wall_now, bool wall_synced, int64_t last_known);

} // namespace lumen::cloud
