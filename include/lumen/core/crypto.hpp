// Lumen — small, portable crypto primitives for local tickets (Lumen Pro P5).
//
// SHA-256 (FIPS 180-4), HMAC-SHA256 (RFC 2104), constant-time comparison and
// base64url without padding (RFC 4648 §5). Portable C++ so the host unit tests
// exercise exactly the code that runs on the controller; verified against the
// FIPS 180-4 and RFC 4231 test vectors and protocol/fixtures/tickets.json.
// Inputs here are short (a ticket is at most 512 bytes), so speed is not a
// concern; no heap allocation except in the std::string results.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace lumen::crypto
{

using Digest = std::array<uint8_t, 32>;

class Sha256
{
  public:
    Sha256();
    void update(const uint8_t *data, std::size_t len);
    void update(const std::string &s) { update(reinterpret_cast<const uint8_t *>(s.data()), s.size()); }
    Digest finish();

  private:
    void block(const uint8_t *p);
    uint32_t h_[8];
    uint8_t buf_[64];
    std::size_t buf_len_ = 0;
    uint64_t total_ = 0;
};

Digest sha256(const uint8_t *data, std::size_t len);
Digest hmac_sha256(const uint8_t *key, std::size_t key_len, const uint8_t *msg, std::size_t msg_len);

// True when the two buffers are equal; time depends only on `len`.
bool equal_ct(const uint8_t *a, const uint8_t *b, std::size_t len);

// base64url without padding. decode() returns false on any character outside
// [A-Za-z0-9_-], a length that cannot occur (len % 4 == 1), or non-zero
// trailing bits (so every byte string has exactly one accepted encoding).
std::string b64url_encode(const uint8_t *data, std::size_t len);
bool b64url_decode(const std::string &in, std::string &out);

} // namespace lumen::crypto
