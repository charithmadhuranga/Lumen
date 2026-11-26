#include "lumen/core/log_capture.hpp"

#include <array>
#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace lumen
{

namespace
{

constexpr std::size_t kLines = 16;
constexpr std::size_t kWidth = 120;

struct Slot
{
    uint32_t uptime_s;
    char level;
    char text[kWidth + 1];
};

std::array<Slot, kLines> g_ring{};
std::size_t g_next = 0;
std::size_t g_count = 0;
uint32_t g_total = 0;
portMUX_TYPE g_mux = portMUX_INITIALIZER_UNLOCKED;
vprintf_like_t g_prev = nullptr;

struct FailSlot
{
    uint32_t uptime_s;
    uint32_t size;
    uint32_t caps;
    char task[configMAX_TASK_NAME_LEN];
};
constexpr std::size_t kFails = 4;
std::array<FailSlot, kFails> g_fails{};
std::size_t g_fail_next = 0;
std::size_t g_fail_count = 0;
uint32_t g_fail_total = 0;

// Runs in the allocating task (any context): record only.
void on_alloc_failed(size_t size, uint32_t caps, const char *)
{
    const char *name = xTaskGetSchedulerState() == taskSCHEDULER_RUNNING ? pcTaskGetName(nullptr) : "boot";
    const uint32_t up = static_cast<uint32_t>(esp_timer_get_time() / 1000000);
    taskENTER_CRITICAL(&g_mux);
    FailSlot &f = g_fails[g_fail_next];
    f.uptime_s = up;
    f.size = static_cast<uint32_t>(size);
    f.caps = caps;
    std::strncpy(f.task, name != nullptr ? name : "?", sizeof(f.task) - 1);
    f.task[sizeof(f.task) - 1] = '\0';
    g_fail_next = (g_fail_next + 1) % kFails;
    if (g_fail_count < kFails) ++g_fail_count;
    ++g_fail_total;
    taskEXIT_CRITICAL(&g_mux);
}

int capture(const char *fmt, va_list args)
{
    va_list copy;
    va_copy(copy, args);
    const int r = g_prev != nullptr ? g_prev(fmt, args) : vprintf(fmt, args);
    // Only the cheap check runs for info/debug lines.
    if (fmt != nullptr && (fmt[0] == 'E' || fmt[0] == 'W') && fmt[1] == ' ' && fmt[2] == '(') {
        char buf[kWidth + 1];
        vsnprintf(buf, sizeof(buf), fmt, copy);
        std::size_t n = std::strlen(buf);
        while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r')) buf[--n] = '\0';
        const uint32_t up = static_cast<uint32_t>(esp_timer_get_time() / 1000000);
        taskENTER_CRITICAL(&g_mux);
        Slot &s = g_ring[g_next];
        s.uptime_s = up;
        s.level = fmt[0];
        std::memcpy(s.text, buf, n + 1);
        g_next = (g_next + 1) % kLines;
        if (g_count < kLines) ++g_count;
        ++g_total;
        taskEXIT_CRITICAL(&g_mux);
    }
    va_end(copy);
    return r;
}

} // namespace

void log_capture_install()
{
    if (g_prev == nullptr) {
        g_prev = esp_log_set_vprintf(&capture);
        const esp_err_t err = heap_caps_register_failed_alloc_callback(&on_alloc_failed);
        if (err != ESP_OK) ESP_LOGW("diag", "failed-alloc hook not installed: %s", esp_err_to_name(err));
    }
}

std::vector<AllocFailure> alloc_failures_recent()
{
    std::array<FailSlot, kFails> snap;
    std::size_t next, count;
    taskENTER_CRITICAL(&g_mux);
    snap = g_fails;
    next = g_fail_next;
    count = g_fail_count;
    taskEXIT_CRITICAL(&g_mux);
    std::vector<AllocFailure> out;
    for (std::size_t i = 0; i < count; ++i) {
        const FailSlot &f = snap[(next + kFails - count + i) % kFails];
        out.push_back(AllocFailure{f.uptime_s, f.size, f.caps, f.task});
    }
    return out;
}

uint32_t alloc_failures_total()
{
    taskENTER_CRITICAL(&g_mux);
    const uint32_t t = g_fail_total;
    taskEXIT_CRITICAL(&g_mux);
    return t;
}

std::vector<LogLine> log_capture_recent()
{
    std::array<Slot, kLines> snap;
    std::size_t next, count;
    taskENTER_CRITICAL(&g_mux);
    snap = g_ring;
    next = g_next;
    count = g_count;
    taskEXIT_CRITICAL(&g_mux);
    std::vector<LogLine> out;
    out.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        const Slot &s = snap[(next + kLines - count + i) % kLines];
        out.push_back(LogLine{s.uptime_s, s.level, s.text});
    }
    return out;
}

uint32_t log_capture_total()
{
    taskENTER_CRITICAL(&g_mux);
    const uint32_t t = g_total;
    taskEXIT_CRITICAL(&g_mux);
    return t;
}

} // namespace lumen
