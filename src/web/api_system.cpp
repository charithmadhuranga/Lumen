// Lumen API — system (SPEC §17.2, §21).
//
//   GET  /system/info          firmware/chip/memory/uptime
//   GET  /system/health        public liveness + per-service health
//   GET  /system/diagnostics   detailed counters (authenticated)
//   POST /system/restart
//   POST /system/factory-reset {confirm: "RESET"}
#include <cstring>

#include "esp_app_desc.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_idf_version.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "lumen/app/app_context.hpp"
#include "lumen/core/model_lock.hpp"
#include "lumen/core/platform.hpp"
#include "lumen/version.hpp"
#include "lumen/web/api_util.hpp"

namespace lumen
{

namespace
{

const char *chip_name(esp_chip_model_t model)
{
    switch (model) {
    case CHIP_ESP32: return "ESP32";
    case CHIP_ESP32S2: return "ESP32-S2";
    case CHIP_ESP32S3: return "ESP32-S3";
    case CHIP_ESP32C3: return "ESP32-C3";
    case CHIP_ESP32C6: return "ESP32-C6";
    default: return "ESP32-variant";
    }
}

const char *reset_reason_name(esp_reset_reason_t r)
{
    switch (r) {
    case ESP_RST_POWERON: return "power_on";
    case ESP_RST_SW: return "software";
    case ESP_RST_PANIC: return "panic";
    case ESP_RST_INT_WDT: return "interrupt_watchdog";
    case ESP_RST_TASK_WDT: return "task_watchdog";
    case ESP_RST_WDT: return "watchdog";
    case ESP_RST_BROWNOUT: return "brownout";
    case ESP_RST_DEEPSLEEP: return "deep_sleep";
    case ESP_RST_EXT: return "external";
    default: return "unknown";
    }
}

} // namespace

void register_system_routes(AppContext &ctx)
{
    WebServer &web = *ctx.web;

    web.route(HTTP_GET, "/system/info", RouteAuth::kControl, [&ctx](ApiRequest &req) {
        esp_chip_info_t chip{};
        esp_chip_info(&chip);
        uint32_t flash = 0;
        esp_flash_get_size(nullptr, &flash);
        FsUsage fs;
        platform_fs_usage(fs);
        const esp_partition_t *running = esp_ota_get_running_partition();
        const esp_app_desc_t *app = esp_app_get_description();
        const NetworkStatus net = ctx.network->status();

        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "firmwareVersion", kVersion);
        cJSON_AddStringToObject(o, "buildDate", app->date);
        cJSON_AddStringToObject(o, "idfVersion", esp_get_idf_version());
        cJSON_AddStringToObject(o, "chipModel", chip_name(chip.model));
        cJSON_AddNumberToObject(o, "chipRevision", chip.revision);
        cJSON_AddNumberToObject(o, "cores", chip.cores);
        cJSON_AddNumberToObject(o, "cpuFreqMHz", CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ);
        cJSON_AddNumberToObject(o, "flashSize", flash);
        cJSON_AddNumberToObject(o, "freeHeap", esp_get_free_heap_size());
        cJSON_AddNumberToObject(o, "minFreeHeap", esp_get_minimum_free_heap_size());
        cJSON_AddNumberToObject(o, "largestFreeBlock", heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT));
        cJSON_AddNumberToObject(o, "uptimeSec", static_cast<double>(esp_timer_get_time() / 1000000));
        cJSON_AddStringToObject(o, "resetReason", reset_reason_name(esp_reset_reason()));
        cJSON_AddNumberToObject(o, "fsTotal", static_cast<double>(fs.total));
        cJSON_AddNumberToObject(o, "fsUsed", static_cast<double>(fs.used));
        cJSON_AddStringToObject(o, "partition", running != nullptr ? running->label : "?");
        cJSON_AddStringToObject(o, "hostname", net.hostname.c_str());
        cJSON_AddStringToObject(o, "mac", net.mac.c_str());
        cJSON_AddStringToObject(o, "board", ctx.boards->active_id().c_str());
        return send_data(req, o);
    });

    // Public health endpoint: liveness only, no sensitive detail.
    web.route(HTTP_GET, "/system/health", RouteAuth::kPublic, [&ctx](ApiRequest &req) {
        cJSON *o = cJSON_CreateObject();
        cJSON *svc = cJSON_AddObjectToObject(o, "services");
        bool hw_ok = false;
        bool lighting_ok = false;
        {
            ModelLock lock(model_mutex());
            hw_ok = ctx.hardware->running() && ctx.hardware->failed_count() == 0;
            lighting_ok = ctx.lighting->hardware_attached();
        }
        const NetworkStatus net = ctx.network->status();
        cJSON_AddBoolToObject(svc, "lighting", lighting_ok);
        cJSON_AddBoolToObject(svc, "hardware", hw_ok);
        cJSON_AddBoolToObject(svc, "network", net.sta_connected || net.eth_link || net.ap_active);
        cJSON_AddBoolToObject(svc, "web", true);
        cJSON_AddBoolToObject(o, "ok", lighting_ok);
        cJSON_AddStringToObject(o, "version", kVersion);
        return send_data(req, o);
    });

    web.route(HTTP_GET, "/system/diagnostics", RouteAuth::kUser, [&ctx](ApiRequest &req) {
        const std::string text = ctx.diagnostics_json();
        cJSON *o = cJSON_Parse(text.c_str());
        return o != nullptr ? send_data(req, o)
                            : send_error(req, 500, "INTERNAL_ERROR", "diagnostics unavailable");
    });

    web.route(HTTP_POST, "/system/restart", RouteAuth::kUser, [&ctx](ApiRequest &req) {
        ctx.schedule_restart(1500);
        cJSON *o = cJSON_CreateObject();
        cJSON_AddBoolToObject(o, "restarting", true);
        return send_data(req, o, 202);
    });

    web.route(HTTP_POST, "/system/factory-reset", RouteAuth::kSession, [&ctx](ApiRequest &req) {
        std::string confirm;
        if (!api::get_string(req.json(), "confirm", confirm, 16) || confirm != "RESET") {
            return send_error(req, 400, "CONFIRMATION_REQUIRED",
                              "Send {\"confirm\":\"RESET\"} to erase all configuration");
        }
        cJSON *o = cJSON_CreateObject();
        cJSON_AddBoolToObject(o, "resetting", true);
        const esp_err_t err = send_data(req, o, 202);
        ctx.factory_reset();
        return err;
    });
}

} // namespace lumen
