// Lumen — ESP-IDF platform ports. See include/lumen/core/platform.hpp
#include "lumen/core/platform.hpp"

#include <new>
#include <sys/stat.h>

#include "esp_littlefs.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "nvs_flash.h"

namespace lumen
{

namespace
{
const char *TAG = "platform";
}

static_assert(sizeof(portMUX_TYPE) <= 16, "portMUX storage too small");

RtosLock::RtosLock()
{
    new (mux_) portMUX_TYPE(portMUX_INITIALIZER_UNLOCKED);
}

void RtosLock::lock()
{
    taskENTER_CRITICAL(reinterpret_cast<portMUX_TYPE *>(mux_));
}

void RtosLock::unlock()
{
    taskEXIT_CRITICAL(reinterpret_cast<portMUX_TYPE *>(mux_));
}

uint32_t uptime_ms()
{
    return static_cast<uint32_t>(esp_timer_get_time() / 1000);
}

uint32_t EspClock::ms()
{
    return uptime_ms();
}

esp_err_t platform_init_nvs()
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS needs erase (%s) - erasing", esp_err_to_name(err));
        esp_err_t e2 = nvs_flash_erase();
        if (e2 != ESP_OK) {
            return e2;
        }
        err = nvs_flash_init();
    }
    return err;
}

esp_err_t platform_mount_fs(bool &formatted)
{
    formatted = false;
    esp_vfs_littlefs_conf_t conf{};
    conf.base_path = kFsBasePath;
    conf.partition_label = kFsPartitionLabel;
    conf.format_if_mount_failed = 0;
    conf.dont_mount = 0;
    esp_err_t err = esp_vfs_littlefs_register(&conf);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "LittleFS mount failed (%s) - formatting", esp_err_to_name(err));
        conf.format_if_mount_failed = 1;
        err = esp_vfs_littlefs_register(&conf);
        formatted = (err == ESP_OK);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "LittleFS unavailable: %s", esp_err_to_name(err));
        return err;
    }
    mkdir(kConfigRoot, 0755);
    FsUsage usage;
    if (platform_fs_usage(usage) == ESP_OK) {
        ESP_LOGI(TAG, "LittleFS mounted at %s: %u/%u bytes used", kFsBasePath,
                 static_cast<unsigned>(usage.used), static_cast<unsigned>(usage.total));
    }
    return ESP_OK;
}

esp_err_t platform_fs_usage(FsUsage &out)
{
    return esp_littlefs_info(kFsPartitionLabel, &out.total, &out.used);
}

} // namespace lumen
