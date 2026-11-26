// Lumen — recent warnings/errors for diagnostics (SPEC §21 "recent errors").
// Hooks esp_log's vprintf: every line still goes to the console; lines that
// start with "E (" or "W (" are also copied (truncated to 120 chars) into a
// 16-entry ring. Log statements never contain secrets (AGENTS.md §5).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace lumen
{

struct LogLine
{
    uint32_t uptime_s = 0;
    char level = 'E';
    std::string text;
};

void log_capture_install();
std::vector<LogLine> log_capture_recent(); // oldest first
uint32_t log_capture_total();              // warnings+errors since boot

// Failed heap allocations (heap_caps failed-alloc hook, installed with the
// log capture). The hook only records; it never allocates or logs.
struct AllocFailure
{
    uint32_t uptime_s = 0;
    uint32_t size = 0;
    uint32_t caps = 0;
    std::string task;
};
std::vector<AllocFailure> alloc_failures_recent(); // last 4, oldest first
uint32_t alloc_failures_total();

} // namespace lumen
