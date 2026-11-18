// Lumen — universal ESP32 smart lighting controller.
// Phase 1: SystemManager service lifecycle replaces the Phase 0 heartbeat
// loop (see docs/ARCHITECTURE.md). Services are registered declaratively and
// driven through init() → start() → stop(). No FreeRTOS task is created
// implicitly here (AGENTS.md §5); that comes when individual services need
// their own task, declared with priority + stack size.
//
// Rules in force (AGENTS.md §5): ESP-IDF only, C++17, check every esp_err_t.
//
// A demonstration heartbeat service is registered so the boot sanity
// indicator keeps working while the SystemManager skeleton is exercised.
#include <cstring>

#include "esp_chip_info.h"
#include "esp_err.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#include "lumen/core/diagnostics.hpp"
#include "lumen/core/service.hpp"
#include "lumen/core/system_manager.hpp"
#include "lumen/version.hpp"

#ifndef LUMEN_FIRMWARE_VERSION
#define LUMEN_FIRMWARE_VERSION "0.0.0"
#endif

static const char *TAG = "lumen";

// Demonstration service: keeps the Phase 0 proof-of-life heartbeat alive
// while the SystemManager drives its lifecycle. Real services (lighting,
// networking, automation) register here in later phases.
class HeartbeatService final : public lumen::IService
{
  public:
    const char *name() const override { return "heartbeat"; }

    lumen::ServiceResult init() override
    {
        ESP_LOGI(TAG, "service %s: init", name());
        return lumen::ServiceResult::kOk;
    }

    lumen::ServiceResult start() override
    {
        ESP_LOGI(TAG, "service %s: start", name());
        return lumen::ServiceResult::kOk;
    }

    lumen::ServiceResult stop() override
    {
        ESP_LOGI(TAG, "service %s: stop (safe state)", name());
        return lumen::ServiceResult::kOk;
    }

    lumen::ServiceStatus status() const override { return status_; }

    void set_safe_state(const lumen::SafeState &state) override
    {
        safe_state_ = state;
    }

    lumen::SafeState safe_state() const override { return safe_state_; }

  private:
    lumen::ServiceStatus status_ = lumen::ServiceStatus::kRegistered;
    lumen::SafeState safe_state_{};
};

extern "C" void app_main(void)
{
    // Boot banner.
    esp_chip_info_t chip{};
    esp_chip_info(&chip);

    uint32_t flash_size = 0;
    esp_err_t err = esp_flash_get_size(nullptr, &flash_size);

    ESP_LOGI(TAG, "**** Lumen firmware v%s ****", lumen::kVersion);
    ESP_LOGI(TAG, "ESP-IDF  : %s", esp_get_idf_version());
    ESP_LOGI(TAG, "Chip     : model %d rev%u, %u core(s) @ %d MHz", (int)chip.model,
             (unsigned)chip.revision, (unsigned)chip.cores,
             CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Flash    : %lu bytes", (unsigned long)flash_size);
    } else {
        ESP_LOGE(TAG, "Flash    : size query failed (%s)", esp_err_to_name(err));
    }
    ESP_LOGI(TAG, "Free heap: %lu bytes (largest block %lu)",
             (unsigned long)esp_get_free_heap_size(),
             (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT));
    ESP_LOGI(TAG, "Boot at  : %lld us", (long long)esp_timer_get_time());

    // SystemManager service lifecycle (Phase 1).
    lumen::SystemManager system;
    HeartbeatService heartbeat;
    system.register_service(heartbeat);

    lumen::ServiceResult init_rc = system.init_all();
    lumen::ServiceResult start_rc = system.start_all();
    ESP_LOGI(TAG, "system: init_all=%d, start_all=%d, services=%u",
             (int)init_rc, (int)start_rc, (unsigned)system.service_count());

    // Phase 1 heartbeat loop: proof of life on real hardware. The SystemManager
    // is now the boot skeleton; services drive their own work in later phases.
    uint32_t beats = 0;
    while (true) {
        ESP_LOGI(TAG, "[heartbeat] up=%lld s, free heap=%lu, beat=%lu",
                 (long long)(esp_timer_get_time() / 1000000),
                 (unsigned long)esp_get_free_heap_size(), (unsigned long)beats++);
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}
