// Lumen — ESP-IDF platform ports (target only).
//
// Concrete ILock / IClock for the event bus, NVS bring-up and the LittleFS
// mount used for config documents and web assets (SPEC §19.1).
#pragma once

#include <cstddef>
#include <cstdint>

#include "esp_err.h"
#include "lumen/core/ports.hpp"

namespace lumen
{

// Mount points (SPEC §19.2): config documents live under /lfs/config,
// pre-gzipped web assets under /lfs/web.
constexpr const char *kFsBasePath = "/lfs";
constexpr const char *kFsPartitionLabel = "storage";
constexpr const char *kConfigRoot = "/lfs/config";
constexpr const char *kWebRoot = "/lfs/web";

// FreeRTOS critical-section lock (portMUX spinlock): microsecond holds only.
class RtosLock final : public ILock
{
  public:
    RtosLock();
    void lock() override;
    void unlock() override;

  private:
    alignas(4) uint8_t mux_[16]; // portMUX_TYPE storage (opaque here)
};

class EspClock final : public IClock
{
  public:
    uint32_t ms() override;
};

uint32_t uptime_ms();

// Initialise NVS; erases and retries when the partition is full or holds a
// newer NVS format (never silently on other errors).
esp_err_t platform_init_nvs();

// Mount LittleFS at kFsBasePath. When the partition cannot be mounted it is
// formatted (first boot / corrupted), reported via `formatted`.
esp_err_t platform_mount_fs(bool &formatted);

struct FsUsage
{
    std::size_t total = 0;
    std::size_t used = 0;
};
esp_err_t platform_fs_usage(FsUsage &out);

} // namespace lumen
