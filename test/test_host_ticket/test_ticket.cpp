// Host tests: crypto primitives (FIPS 180-4, RFC 4231) and local ticket
// verification against protocol/fixtures/tickets.json (Lumen Pro P5).
#include <cstdio>
#include <string>
#include <vector>

#include <unity.h>

#include "core/cJSON.h"
#include "lumen/core/crypto.hpp"
#include "lumen/services/ticket.hpp"

using namespace lumen;

extern "C" void setUp(void) {}
extern "C" void tearDown(void) {}

namespace
{

std::string hex(const uint8_t *p, std::size_t n)
{
    static const char *d = "0123456789abcdef";
    std::string s;
    for (std::size_t i = 0; i < n; ++i) {
        s += d[p[i] >> 4];
        s += d[p[i] & 15];
    }
    return s;
}

std::vector<uint8_t> unhex(const std::string &h)
{
    std::vector<uint8_t> out;
    for (std::size_t i = 0; i + 1 < h.size(); i += 2) out.push_back(uint8_t(std::stoi(h.substr(i, 2), nullptr, 16)));
    return out;
}

std::string sha_hex(const std::string &m)
{
    const auto d = crypto::sha256(reinterpret_cast<const uint8_t *>(m.data()), m.size());
    return hex(d.data(), d.size());
}

std::string hmac_hex(const std::vector<uint8_t> &key, const std::string &msg)
{
    const auto d = crypto::hmac_sha256(key.data(), key.size(), reinterpret_cast<const uint8_t *>(msg.data()), msg.size());
    return hex(d.data(), d.size());
}

std::string read_file(const std::string &path)
{
    FILE *f = std::fopen(path.c_str(), "rb");
    TEST_ASSERT_NOT_NULL_MESSAGE(f, path.c_str());
    std::string s;
    char buf[4096];
    std::size_t n;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) s.append(buf, n);
    std::fclose(f);
    return s;
}

} // namespace

// --- SHA-256: FIPS 180-4 examples ---------------------------------------------------------

void test_sha256_vectors()
{
    TEST_ASSERT_EQUAL_STRING("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855", sha_hex("").c_str());
    TEST_ASSERT_EQUAL_STRING("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", sha_hex("abc").c_str());
    TEST_ASSERT_EQUAL_STRING("248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1",
                             sha_hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq").c_str());
    TEST_ASSERT_EQUAL_STRING("cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0",
                             sha_hex(std::string(1000000, 'a')).c_str());
    // Streaming in odd chunks gives the same digest.
    crypto::Sha256 s;
    const std::string m = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    for (std::size_t i = 0; i < m.size(); i += 7) s.update(m.substr(i, 7));
    const auto d = s.finish();
    TEST_ASSERT_EQUAL_STRING("248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1", hex(d.data(), 32).c_str());
}

// --- HMAC-SHA256: RFC 4231 test cases 1-4, 6, 7 ------------------------------------------------

void test_hmac_rfc4231()
{
    TEST_ASSERT_EQUAL_STRING("b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7",
                             hmac_hex(std::vector<uint8_t>(20, 0x0b), "Hi There").c_str());
    TEST_ASSERT_EQUAL_STRING("5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843",
                             hmac_hex({'J', 'e', 'f', 'e'}, "what do ya want for nothing?").c_str());
    TEST_ASSERT_EQUAL_STRING("773ea91e36800e46854db8ebd09181a72959098b3ef8c122d9635514ced565fe",
                             hmac_hex(std::vector<uint8_t>(20, 0xaa), std::string(50, '\xdd')).c_str());
    TEST_ASSERT_EQUAL_STRING("82558a389a443c0ea4cc819899f2083a85f0faa3e578f8077a2e3ff46729665b",
                             hmac_hex(unhex("0102030405060708090a0b0c0d0e0f10111213141516171819"), std::string(50, '\xcd')).c_str());
    TEST_ASSERT_EQUAL_STRING("60e431591ee0b67f0d8a26aacbf5b77f8e0bc6213728c5140546040f0ee37f54",
                             hmac_hex(std::vector<uint8_t>(131, 0xaa), "Test Using Larger Than Block-Size Key - Hash Key First").c_str());
    TEST_ASSERT_EQUAL_STRING(
        "9b09ffa71b942fcb27635fbcd5b0e944bfdc63644f0713938a7f51535c3a35e2",
        hmac_hex(std::vector<uint8_t>(131, 0xaa),
                 "This is a test using a larger than block-size key and a larger than block-size data. The key needs to "
                 "be hashed before being used by the HMAC algorithm.")
            .c_str());
}

void test_equal_ct()
{
    const uint8_t a[] = {1, 2, 3}, b[] = {1, 2, 3}, c[] = {1, 2, 4};
    TEST_ASSERT_TRUE(crypto::equal_ct(a, b, 3));
    TEST_ASSERT_FALSE(crypto::equal_ct(a, c, 3));
}

void test_b64url()
{
    const std::string cases[] = {"", "f", "fo", "foo", "foob", "fooba", "foobar", std::string("\xff\xfe\x00\x01", 4)};
    for (const auto &c : cases) {
        const std::string enc = crypto::b64url_encode(reinterpret_cast<const uint8_t *>(c.data()), c.size());
        TEST_ASSERT_EQUAL(std::string::npos, enc.find('='));
        std::string dec;
        TEST_ASSERT_TRUE(crypto::b64url_decode(enc, dec));
        TEST_ASSERT_TRUE(dec == c);
    }
    TEST_ASSERT_EQUAL_STRING("Zm9vYmFy", crypto::b64url_encode(reinterpret_cast<const uint8_t *>("foobar"), 6).c_str());
    TEST_ASSERT_EQUAL_STRING("__79AAE", crypto::b64url_encode(reinterpret_cast<const uint8_t *>("\xff\xfe\xfd\x00\x01"), 5).c_str());
    std::string out;
    TEST_ASSERT_FALSE(crypto::b64url_decode("Zm9v+g", out));  // standard alphabet
    TEST_ASSERT_FALSE(crypto::b64url_decode("Zm9vYg==", out)); // padding
    TEST_ASSERT_FALSE(crypto::b64url_decode("Z", out));        // impossible length
    TEST_ASSERT_FALSE(crypto::b64url_decode("Zh", out));       // "f" is "Zg": non-zero trailing bits
    TEST_ASSERT_TRUE(crypto::b64url_decode("Zg", out));
}

// --- tickets: protocol/fixtures/tickets.json -----------------------------------------------------

void test_protocol_ticket_vectors()
{
    const std::string text = read_file(std::string(LUMEN_REPO_ROOT) + "/protocol/fixtures/tickets.json");
    cJSON *v = cJSON_Parse(text.c_str());
    TEST_ASSERT_NOT_NULL(v);
    const std::vector<uint8_t> key = unhex(cJSON_GetObjectItem(v, "key")->valuestring);
    TEST_ASSERT_EQUAL(32, key.size());
    const std::string device = cJSON_GetObjectItem(v, "device")->valuestring;
    const uint32_t epoch = uint32_t(cJSON_GetObjectItem(v, "epoch")->valuedouble);
    const int64_t now = int64_t(cJSON_GetObjectItem(v, "now")->valuedouble);
    int valid = 0, invalid = 0;
    const cJSON *c = nullptr;
    cJSON_ArrayForEach(c, cJSON_GetObjectItem(v, "valid"))
    {
        const char *name = cJSON_GetObjectItem(c, "name")->valuestring;
        const auto r = ticket::verify(cJSON_GetObjectItem(c, "ticket")->valuestring, key.data(), device, epoch, now);
        TEST_ASSERT_TRUE_MESSAGE(r.ok(), (std::string(name) + ": " + ticket::reason_name(r.reason)).c_str());
        const cJSON *claims = cJSON_GetObjectItem(c, "claims");
        TEST_ASSERT_EQUAL_STRING_MESSAGE(cJSON_GetObjectItem(claims, "sub")->valuestring, r.claims.sub.c_str(), name);
        TEST_ASSERT_EQUAL_INT64(int64_t(cJSON_GetObjectItem(claims, "exp")->valuedouble), r.claims.exp);
        const bool admin = std::string(cJSON_GetObjectItem(claims, "scp")->valuestring) == "admin";
        TEST_ASSERT_TRUE(admin == (r.claims.scope == ticket::Scope::kAdmin));
        ++valid;
    }
    cJSON_ArrayForEach(c, cJSON_GetObjectItem(v, "invalid"))
    {
        const char *name = cJSON_GetObjectItem(c, "name")->valuestring;
        const auto r = ticket::verify(cJSON_GetObjectItem(c, "ticket")->valuestring, key.data(), device, epoch, now);
        TEST_ASSERT_EQUAL_STRING_MESSAGE(cJSON_GetObjectItem(c, "reason")->valuestring, ticket::reason_name(r.reason), name);
        ++invalid;
    }
    TEST_ASSERT_TRUE(valid >= 3);
    TEST_ASSERT_TRUE(invalid >= 14);
    cJSON_Delete(v);
}

void test_wrong_key_length_material_never_verifies()
{
    // A ticket made with the right claims but a different key must fail with "mac".
    const std::vector<uint8_t> key(32, 0x11);
    const auto r = ticket::verify("LT1.e30.AAAA", key.data(), "x", 0, 0);
    TEST_ASSERT_EQUAL_STRING("mac", ticket::reason_name(r.reason));
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_sha256_vectors);
    RUN_TEST(test_hmac_rfc4231);
    RUN_TEST(test_equal_ct);
    RUN_TEST(test_b64url);
    RUN_TEST(test_protocol_ticket_vectors);
    RUN_TEST(test_wrong_key_length_material_never_verifies);
    return UNITY_END();
}
