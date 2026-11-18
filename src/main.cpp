// Lumen — universal ESP32 smart lighting controller.
// Phase 0: minimal boot application. Later phases replace the heartbeat
// loop with the SystemManager service lifecycle (see docs/ARCHITECTURE.md).
//
// Rules in force (AGENTS.md §5): ESP-IDF only, C++17, check every
// esp_err_t, no blocking delays in lighting code (this heartbeat is a boot
// sanity indicator, not lighting code).

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

#include "lumen/version.hpp"

static const char *TAG = "lumen";

extern "C" void app_main(void)
{
    // Boot banner — used by the on-device Phase 0 test on the KC868-AP.
    esp_chip_info_t chip{};
    esp_chip_info(&chip);

    uint32_t flash_size = 0;
    esp_err_t err = esp_flash_get_size(nullptr, &flash_size);

    ESP_LOGI(TAG, "**** Lumen firmware v%s ****", lumen::kVersion);
    ESP_LOGI(TAG, "ESP-IDF  : %s", esp_get_idf_version());
    ESP_LOGI(TAG, "Chip     : model %d rev%u, %u core(s) @ %d MHz", (int)chip.model,
             (unsigned)chip.revision, (unsigned)chip.cores, CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Flash    : %lu bytes", (unsigned long)flash_size);
    } else {
        ESP_LOGE(TAG, "Flash    : size query failed (%s)", esp_err_to_name(err));
    }
    ESP_LOGI(TAG, "Free heap: %lu bytes (largest block %lu)",
             (unsigned long)esp_get_free_heap_size(),
             (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT));
    ESP_LOGI(TAG, "Boot at  : %lld us", (long long)esp_timer_get_time());

    // Phase 0 heartbeat: proof of life on real hardware. Replaced in Phase 1
    // by the FreeRTOS service tasks documented in docs/ARCHITECTURE.md.
    uint32_t beats = 0;
    while (true) {
        ESP_LOGI(TAG, "[heartbeat] up=%lld s, free heap=%lu, beat=%lu",
                 (long long)(esp_timer_get_time() / 1000000),
                 (unsigned long)esp_get_free_heap_size(), (unsigned long)beats++);
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}
