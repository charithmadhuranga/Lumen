// Host tests: ESP-NOW protocol v1 envelope, authentication and replay rules.
#include <string>
#include <vector>

#include <unity.h>

#include "lumen/networking/espnow_protocol.h"

using namespace lumen::espnow;

extern "C" void setUp(void) {}
extern "C" void tearDown(void) {}

namespace
{
const std::string kKey = "0123456789abcdef0123456789abcdef";
const std::array<uint8_t, 6> kA = {1, 2, 3, 4, 5, 6};
const std::array<uint8_t, 6> kB = {6, 5, 4, 3, 2, 1};

Frame frame(uint32_t seq, uint32_t id, Type t = Type::kState, std::string payload = R"({"id":"ceiling","p":1,"b":40})")
{
    Frame f;
    f.type = t;
    f.flags = kFlagTsValid;
    f.network = network_tag("home");
    f.seq = seq;
    f.msg_id = id;
    f.timestamp = 1800000000;
    f.payload = std::move(payload);
    return f;
}
} // namespace

static void test_round_trip_and_size_limit(void)
{
    std::vector<uint8_t> bytes;
    TEST_ASSERT_TRUE(encode(frame(7, 99), kKey, bytes));
    TEST_ASSERT_TRUE(bytes.size() <= kMaxFrame);
    Frame out;
    TEST_ASSERT_EQUAL(static_cast<int>(Reject::kNone), static_cast<int>(decode(bytes.data(), bytes.size(), kKey, network_tag("home"), out)));
    TEST_ASSERT_EQUAL_UINT32(7, out.seq);
    TEST_ASSERT_EQUAL_UINT32(99, out.msg_id);
    TEST_ASSERT_EQUAL_STRING(R"({"id":"ceiling","p":1,"b":40})", out.payload.c_str());
    TEST_ASSERT_TRUE(encode(frame(1, 1, Type::kState, std::string(kMaxPayload, 'x')), kKey, bytes));
    TEST_ASSERT_EQUAL(kMaxFrame, bytes.size());
    TEST_ASSERT_FALSE(encode(frame(1, 1, Type::kState, std::string(kMaxPayload + 1, 'x')), kKey, bytes));
}

static void test_rejects_bad_frames(void)
{
    std::vector<uint8_t> good;
    encode(frame(1, 1), kKey, good);
    Frame out;
    auto rej = [&](std::vector<uint8_t> b, const std::string &key = kKey, uint32_t net = network_tag("home")) {
        return static_cast<int>(decode(b.data(), b.size(), key, net, out));
    };
    TEST_ASSERT_EQUAL(static_cast<int>(Reject::kAuth), rej(good, "wrong-key"));
    TEST_ASSERT_EQUAL(static_cast<int>(Reject::kNetwork), rej(good, kKey, network_tag("neighbour")));
    auto tampered = good;
    tampered[30] ^= 0x01; // payload byte
    TEST_ASSERT_EQUAL(static_cast<int>(Reject::kAuth), rej(tampered));
    auto seq_changed = good;
    seq_changed[10] ^= 0x01;
    TEST_ASSERT_EQUAL(static_cast<int>(Reject::kAuth), rej(seq_changed));
    auto v2 = good;
    v2[2] = 2;
    TEST_ASSERT_EQUAL(static_cast<int>(Reject::kVersion), rej(v2));
    auto magic = good;
    magic[0] = 'X';
    TEST_ASSERT_EQUAL(static_cast<int>(Reject::kMalformed), rej(magic));
    auto truncated = good;
    truncated.pop_back();
    TEST_ASSERT_EQUAL(static_cast<int>(Reject::kMalformed), rej(truncated));
    auto lying_len = good;
    lying_len[22] = 200; // claims a longer payload than present
    TEST_ASSERT_EQUAL(static_cast<int>(Reject::kMalformed), rej(lying_len));
    auto bad_type = good;
    bad_type[3] = 9;
    TEST_ASSERT_EQUAL(static_cast<int>(Reject::kMalformed), rej(bad_type));
    std::vector<uint8_t> oversize(kMaxFrame + 1, 0);
    TEST_ASSERT_EQUAL(static_cast<int>(Reject::kMalformed), rej(oversize));
    TEST_ASSERT_EQUAL(static_cast<int>(Reject::kMalformed), rej(std::vector<uint8_t>{}));
}

static void test_replay_duplicate_and_stale(void)
{
    ReplayFilter rf;
    const int64_t now = 1800000000;
    TEST_ASSERT_EQUAL(0, static_cast<int>(rf.check(kA, frame(10, 1), now, true)));
    TEST_ASSERT_EQUAL(static_cast<int>(ReplayFilter::Verdict::kDuplicate), static_cast<int>(rf.check(kA, frame(11, 1), now, true)));
    TEST_ASSERT_EQUAL(static_cast<int>(ReplayFilter::Verdict::kReplay), static_cast<int>(rf.check(kA, frame(10, 2), now, true)));
    TEST_ASSERT_EQUAL(static_cast<int>(ReplayFilter::Verdict::kReplay), static_cast<int>(rf.check(kA, frame(9, 3), now, true)));
    TEST_ASSERT_EQUAL(0, static_cast<int>(rf.check(kA, frame(11, 4), now, true)));
    // Sequences are per sender.
    TEST_ASSERT_EQUAL(0, static_cast<int>(rf.check(kB, frame(1, 5), now, true)));
    // Timestamp window, only when both clocks are valid.
    TEST_ASSERT_EQUAL(static_cast<int>(ReplayFilter::Verdict::kStale), static_cast<int>(rf.check(kA, frame(20, 6), now + 600, true)));
    TEST_ASSERT_EQUAL(0, static_cast<int>(rf.check(kA, frame(21, 7), now + 600, false)));
    Frame no_clock = frame(22, 8);
    no_clock.flags = 0;
    TEST_ASSERT_EQUAL(0, static_cast<int>(rf.check(kA, no_clock, now + 600, true)));
    // A rebooted sender continues above its old sequence (boot count in the high bits).
    TEST_ASSERT_EQUAL(0, static_cast<int>(rf.check(kA, frame((2u << 20) | 1, 9), now, true)));
}

static void test_sender_limit(void)
{
    ReplayFilter rf;
    for (uint8_t i = 0; i < ReplayFilter::kSenders; ++i) {
        std::array<uint8_t, 6> mac = {0, 0, 0, 0, 0, i};
        TEST_ASSERT_EQUAL(0, static_cast<int>(rf.check(mac, frame(1, 100 + i), 0, false)));
    }
    std::array<uint8_t, 6> extra = {9, 9, 9, 9, 9, 9};
    TEST_ASSERT_EQUAL(static_cast<int>(ReplayFilter::Verdict::kFull), static_cast<int>(rf.check(extra, frame(1, 500), 0, false)));
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_round_trip_and_size_limit);
    RUN_TEST(test_rejects_bad_frames);
    RUN_TEST(test_replay_duplicate_and_stale);
    RUN_TEST(test_sender_limit);
    return UNITY_END();
}
