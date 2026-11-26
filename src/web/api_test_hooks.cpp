// Lumen API — fault injection for the on-device reliability suite
// (test/integration/reliability_test.py). Compiled in ONLY when the image is
// built with LUMEN_TEST_HOOKS=1 (scripts/pio_version.py); release images and
// the release workflow never set it, and such an image says so in
// /system/diagnostics ("testHooks": true) and in a boot warning.
//
//   GET    /test/hooks                       {"heapReserved": n}
//   POST   /test/heap/reserve  {leaveBytes}  allocate until ~leaveBytes stay free
//   POST   /test/heap/release                free everything reserved
//   POST   /test/i2c/fault     {address, fail}  every transaction to address fails
//   POST   /test/i2c/write     {address, reg, value}  raw register write
//   POST   /test/i2c/read      {address, reg}         raw register read
//   POST   /test/espnow/hold   {ms}                   delay every received ESP-NOW frame
#include "lumen/app/app_context.hpp"

#ifdef LUMEN_TEST_HOOKS
#include <cstdlib>
#include <mutex>
#include <vector>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "lumen/hardware/drivers/i2c_manager.h"
#include "lumen/networking/espnow_manager.hpp"
#include "lumen/web/api_util.hpp"
#endif

namespace lumen
{

#ifndef LUMEN_TEST_HOOKS

void register_test_hook_routes(AppContext &) {}

#else

namespace
{

constexpr std::size_t kChunk = 2048;
constexpr std::size_t kMaxReserve = 256 * 1024;
std::mutex g_heap_mutex;
std::vector<void *> g_chunks; // reserved up front: no growth while low

std::size_t reserved_bytes()
{
    return g_chunks.size() * kChunk;
}

I2cManager *bus(AppContext &ctx)
{
    return static_cast<I2cManager *>(ctx.hardware->i2c_bus(""));
}

bool address_arg(ApiRequest &req, const char *key, int &out, int max)
{
    double v = 0;
    if (!api::get_number(req.json(), key, v) || v < 0 || v > max) return false;
    out = static_cast<int>(v);
    return true;
}

} // namespace

void register_test_hook_routes(AppContext &ctx)
{
    ESP_LOGW("test", "*** TEST IMAGE: fault-injection routes /api/v1/test/* are enabled — not for production ***");
    g_chunks.reserve(kMaxReserve / kChunk);
    WebServer &web = *ctx.web;

    web.route(HTTP_GET, "/test/hooks", RouteAuth::kUser, [](ApiRequest &req) {
        std::lock_guard<std::mutex> lock(g_heap_mutex);
        cJSON *o = cJSON_CreateObject();
        cJSON_AddNumberToObject(o, "heapReserved", static_cast<double>(reserved_bytes()));
        return send_data(req, o);
    });

    web.route(HTTP_POST, "/test/heap/reserve", RouteAuth::kUser, [](ApiRequest &req) {
        double leave = 0;
        if (!api::get_number(req.json(), "leaveBytes", leave) || leave < 4096) return send_error(req, 400, "INVALID_REQUEST", "leaveBytes >= 4096 required");
        std::lock_guard<std::mutex> lock(g_heap_mutex);
        while (reserved_bytes() < kMaxReserve && esp_get_free_heap_size() > leave + kChunk) {
            void *p = heap_caps_malloc(kChunk, MALLOC_CAP_8BIT);
            if (p == nullptr) break;
            g_chunks.push_back(p);
        }
        cJSON *o = cJSON_CreateObject();
        cJSON_AddNumberToObject(o, "heapReserved", static_cast<double>(reserved_bytes()));
        cJSON_AddNumberToObject(o, "freeHeap", esp_get_free_heap_size());
        return send_data(req, o);
    });

    web.route(HTTP_POST, "/test/heap/release", RouteAuth::kUser, [](ApiRequest &req) {
        std::lock_guard<std::mutex> lock(g_heap_mutex);
        for (void *p : g_chunks) heap_caps_free(p);
        g_chunks.clear();
        cJSON *o = cJSON_CreateObject();
        cJSON_AddNumberToObject(o, "freeHeap", esp_get_free_heap_size());
        return send_data(req, o);
    });

    web.route(HTTP_POST, "/test/i2c/fault", RouteAuth::kUser, [&ctx](ApiRequest &req) {
        int addr = 0;
        if (!address_arg(req, "address", addr, 0x7F) || bus(ctx) == nullptr) return send_error(req, 400, "INVALID_REQUEST", "address 0..127 and an I2C bus required");
        const bool fail = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(req.json(), "fail"));
        bus(ctx)->inject_fault(static_cast<uint8_t>(addr), fail ? ESP_ERR_TIMEOUT : ESP_OK);
        cJSON *o = cJSON_CreateObject();
        cJSON_AddBoolToObject(o, "failing", fail);
        return send_data(req, o);
    });

    web.route(HTTP_POST, "/test/i2c/write", RouteAuth::kUser, [&ctx](ApiRequest &req) {
        int addr = 0, reg = 0, value = 0;
        if (!address_arg(req, "address", addr, 0x7F) || !address_arg(req, "reg", reg, 0xFF) || !address_arg(req, "value", value, 0xFF) || bus(ctx) == nullptr)
            return send_error(req, 400, "INVALID_REQUEST", "address, reg, value required");
        const uint8_t buf[2] = {static_cast<uint8_t>(reg), static_cast<uint8_t>(value)};
        const esp_err_t err = bus(ctx)->write(static_cast<uint8_t>(addr), buf, 2);
        if (err != ESP_OK) return send_error(req, 502, "I2C_ERROR", esp_err_to_name(err));
        return send_data(req, cJSON_CreateObject());
    });

    web.route(HTTP_POST, "/test/espnow/hold", RouteAuth::kUser, [](ApiRequest &req) {
        double ms = 0;
        if (!api::get_number(req.json(), "ms", ms) || ms < 0 || ms > 3000) return send_error(req, 400, "INVALID_REQUEST", "ms 0..3000 required");
        EspNowManager::test_set_rx_hold_ms(static_cast<uint32_t>(ms));
        cJSON *o = cJSON_CreateObject();
        cJSON_AddNumberToObject(o, "ms", ms);
        return send_data(req, o);
    });

    web.route(HTTP_POST, "/test/i2c/read", RouteAuth::kUser, [&ctx](ApiRequest &req) {
        int addr = 0, reg = 0;
        if (!address_arg(req, "address", addr, 0x7F) || !address_arg(req, "reg", reg, 0xFF) || bus(ctx) == nullptr)
            return send_error(req, 400, "INVALID_REQUEST", "address, reg required");
        const uint8_t r = static_cast<uint8_t>(reg);
        uint8_t v = 0;
        const esp_err_t err = bus(ctx)->write_then_read(static_cast<uint8_t>(addr), &r, 1, &v, 1);
        if (err != ESP_OK) return send_error(req, 502, "I2C_ERROR", esp_err_to_name(err));
        cJSON *o = cJSON_CreateObject();
        cJSON_AddNumberToObject(o, "value", v);
        return send_data(req, o);
    });
}

#endif

} // namespace lumen
