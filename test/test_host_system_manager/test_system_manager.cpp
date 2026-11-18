// Host tests for the SystemManager: service registration, bounded init,
// start/stop order, and failure isolation (one failing service does not
// prevent the rest from booting).
#include <cstring>
#include <string>
#include <vector>

#include <unity.h>

#include "lumen/core/service.hpp"
#include "lumen/core/system_manager.hpp"

using lumen::IService;
using lumen::SafeState;
using lumen::ServiceResult;
using lumen::ServiceStatus;
using lumen::SystemManager;

namespace
{

struct Call
{
    std::string name;
    int init_calls = 0;
    int start_calls = 0;
    int stop_calls = 0;
    bool should_fail_init = false;
    bool should_fail_start = false;
    ServiceStatus status_ = ServiceStatus::kRegistered;
    SafeState safe_state_{};
};

std::vector<Call> g_calls;

class MockService final : public IService
{
  public:
    explicit MockService(const char *name) : name_(name)
    {
        Call c;
        c.name = name;
        g_calls.push_back(c);
        index_ = g_calls.size() - 1;
    }

    const char *name() const override { return name_.c_str(); }

    ServiceResult init() override
    {
        g_calls[index_].init_calls++;
        status_ = should_fail_init ? ServiceStatus::kFailed
                                  : ServiceStatus::kInitialized;
        return should_fail_init ? ServiceResult::kFailed
                                : ServiceResult::kOk;
    }

    ServiceResult start() override
    {
        g_calls[index_].start_calls++;
        status_ = should_fail_start ? ServiceStatus::kFailed
                                   : ServiceStatus::kStarted;
        return should_fail_start ? ServiceResult::kFailed
                                 : ServiceResult::kOk;
    }

    ServiceResult stop() override
    {
        g_calls[index_].stop_calls++;
        safe_state_.active = true;
        safe_state_.value = 0;
        status_ = ServiceStatus::kStopped;
        return ServiceResult::kOk;
    }

    ServiceStatus status() const override { return status_; }

    void set_safe_state(const SafeState &state) override
    {
        g_calls[index_].safe_state_ = state;
        safe_state_ = state;
    }

    SafeState safe_state() const override { return safe_state_; }

    void set_fail_init(bool v) { should_fail_init = v; }
    void set_fail_start(bool v) { should_fail_start = v; }

  private:
    std::string name_;
    std::size_t index_ = 0;
    bool should_fail_init = false;
    bool should_fail_start = false;
    ServiceStatus status_ = ServiceStatus::kRegistered;
    SafeState safe_state_{};
};

} // namespace

extern "C" void setUp(void)
{
    g_calls.clear();
}

extern "C" void tearDown(void) {}

// ---------------------------------------------------------------------------
static void test_register_and_duplicate_rejection(void)
{
    SystemManager sm;
    MockService a("alpha");
    MockService b("beta");

    TEST_ASSERT_TRUE(sm.register_service(a));
    TEST_ASSERT_TRUE(sm.register_service(b));
    TEST_ASSERT_TRUE(sm.register_service(a) == false); // duplicate name
    TEST_ASSERT_EQUAL_size_t(2, sm.service_count());
}

static void test_registry_is_bounded(void)
{
    SystemManager sm;
    std::vector<MockService> services;
    services.reserve(16);
    for (int i = 0; i < 16; ++i) {
        char name[16];
        std::snprintf(name, sizeof(name), "svc%d", i);
        services.emplace_back(name);
        TEST_ASSERT_TRUE(sm.register_service(services.back()));
    }
    // 17th service exceeds kMaxServices (16).
    MockService overflow("overflow");
    TEST_ASSERT_FALSE(sm.register_service(overflow));
    TEST_ASSERT_EQUAL_size_t(16, sm.service_count());
}

static void test_find_service(void)
{
    SystemManager sm;
    MockService a("alpha");
    MockService b("beta");
    sm.register_service(a);
    sm.register_service(b);

    TEST_ASSERT_NOT_NULL(sm.find_service("alpha"));
    TEST_ASSERT_NOT_NULL(sm.find_service("beta"));
    TEST_ASSERT_NULL(sm.find_service("gamma"));
    TEST_ASSERT_NULL(sm.find_service(nullptr));
}

static void test_init_all_starts_all_services(void)
{
    SystemManager sm;
    MockService a("alpha");
    MockService b("beta");
    sm.register_service(a);
    sm.register_service(b);

    TEST_ASSERT_EQUAL(ServiceResult::kOk, sm.init_all());
    TEST_ASSERT_FALSE(sm.any_failed());

    TEST_ASSERT_EQUAL(ServiceStatus::kInitialized, a.status());
    TEST_ASSERT_EQUAL(ServiceStatus::kInitialized, b.status());
    TEST_ASSERT_EQUAL_INT(1, g_calls[0].init_calls);
    TEST_ASSERT_EQUAL_INT(1, g_calls[1].init_calls);
}

static void test_init_all_bounded_startup_on_failure(void)
{
    SystemManager sm;
    MockService a("alpha");
    MockService b("beta");
    b.set_fail_init(true); // beta fails to initialize
    sm.register_service(a);
    sm.register_service(b);

    TEST_ASSERT_EQUAL(ServiceResult::kFailed, sm.init_all());
    TEST_ASSERT_TRUE(sm.any_failed());

    // alpha initialized despite beta failing (bounded startup).
    TEST_ASSERT_EQUAL(ServiceStatus::kInitialized, a.status());
    TEST_ASSERT_EQUAL(ServiceStatus::kFailed, b.status());
}

static void test_start_all_after_init(void)
{
    SystemManager sm;
    MockService a("alpha");
    MockService b("beta");
    sm.register_service(a);
    sm.register_service(b);
    sm.init_all();

    TEST_ASSERT_EQUAL(ServiceResult::kOk, sm.start_all());
    TEST_ASSERT_EQUAL(ServiceStatus::kStarted, a.status());
    TEST_ASSERT_EQUAL(ServiceStatus::kStarted, b.status());
    TEST_ASSERT_EQUAL_INT(1, g_calls[0].start_calls);
    TEST_ASSERT_EQUAL_INT(1, g_calls[1].start_calls);
}

static void test_stop_all_runs_in_reverse_order(void)
{
    SystemManager sm;
    MockService a("alpha"); // index 0
    MockService b("beta");  // index 1
    MockService c("gamma"); // index 2
    sm.register_service(a);
    sm.register_service(b);
    sm.register_service(c);
    sm.init_all();
    sm.start_all();

    TEST_ASSERT_EQUAL(ServiceResult::kOk, sm.stop_all());

    // Stop order must be reverse: gamma, beta, alpha.
    TEST_ASSERT_EQUAL_INT(1, g_calls[2].stop_calls); // gamma first
    TEST_ASSERT_EQUAL_INT(1, g_calls[1].stop_calls); // beta second
    TEST_ASSERT_EQUAL_INT(1, g_calls[0].stop_calls); // alpha last

    TEST_ASSERT_EQUAL(ServiceStatus::kStopped, a.status());
    TEST_ASSERT_EQUAL(ServiceStatus::kStopped, b.status());
    TEST_ASSERT_EQUAL(ServiceStatus::kStopped, c.status());

    // Safe states engaged on stop.
    TEST_ASSERT_TRUE(a.safe_state().active);
    TEST_ASSERT_TRUE(b.safe_state().active);
    TEST_ASSERT_TRUE(c.safe_state().active);
}

static void test_safe_state_configuration(void)
{
    SystemManager sm;
    MockService a("alpha");
    a.set_safe_state({true, 0});
    sm.register_service(a);
    sm.init_all();
    sm.start_all();
    sm.stop_all();

    TEST_ASSERT_TRUE(a.safe_state().active);
    TEST_ASSERT_EQUAL_INT32(0, a.safe_state().value);
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_register_and_duplicate_rejection);
    RUN_TEST(test_registry_is_bounded);
    RUN_TEST(test_find_service);
    RUN_TEST(test_init_all_starts_all_services);
    RUN_TEST(test_init_all_bounded_startup_on_failure);
    RUN_TEST(test_start_all_after_init);
    RUN_TEST(test_stop_all_runs_in_reverse_order);
    RUN_TEST(test_safe_state_configuration);
    return UNITY_END();
}
