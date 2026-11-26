// Lumen — firmware OTA with rollback, and web UI updates. See ota_service.hpp.
#include "lumen/app/ota_service.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <memory>
#include <sys/stat.h>
#include <unistd.h>

#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "lumen/core/platform.hpp"
#include "lumen/services/ota_image.h"
#include "lumen/web/web_pack.hpp"
#include "sdkconfig.h"

namespace lumen
{

namespace
{

constexpr const char *TAG = "ota";
constexpr const char *kProject = "lumen-firmware";
constexpr std::size_t kChunk = 4096;
constexpr int kMaxTimeouts = 3; // × recv_wait_timeout (10 s)
constexpr std::size_t kMaxUiPack = 600 * 1024;
constexpr std::size_t kFsMargin = 24 * 1024; // keep room for config writes

const char *state_name(esp_ota_img_states_t s)
{
    switch (s) {
    case ESP_OTA_IMG_NEW: return "new";
    case ESP_OTA_IMG_PENDING_VERIFY: return "pending_verify";
    case ESP_OTA_IMG_VALID: return "valid";
    case ESP_OTA_IMG_INVALID: return "invalid";
    case ESP_OTA_IMG_ABORTED: return "aborted";
    default: return "undefined";
    }
}

// Reads exactly `len` bytes (or fewer at end of body); -1 on socket error.
int recv_some(httpd_req_t *req, uint8_t *buf, std::size_t len, int &timeouts)
{
    for (;;) {
        const int n = httpd_req_recv(req, reinterpret_cast<char *>(buf), len);
        if (n == HTTPD_SOCK_ERR_TIMEOUT) {
            if (++timeouts > kMaxTimeouts) return -1;
            continue;
        }
        return n;
    }
}

} // namespace

void OtaService::boot()
{
#ifdef LUMEN_OTA_SELFTEST
    ESP_LOGW(TAG, "*** OTA SELF-TEST IMAGE (rollback drill %d) - not for release ***", LUMEN_OTA_SELFTEST);
#endif
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t state = ESP_OTA_IMG_UNDEFINED;
    if (running != nullptr && esp_ota_get_state_partition(running, &state) == ESP_OK) {
        pending_verify_ = state == ESP_OTA_IMG_PENDING_VERIFY;
    }
    ESP_LOGI(TAG, "running %s (%s), firmware %s%s", running ? running->label : "?", state_name(state),
             esp_app_get_description()->version, pending_verify_ ? " — awaiting health check" : "");
}

OtaStatus OtaService::status() const
{
    OtaStatus s;
    {
        std::lock_guard<std::mutex> l(mutex_);
        s = st_;
    }
    const esp_partition_t *running = esp_ota_get_running_partition();
    const esp_partition_t *other = esp_ota_get_next_update_partition(nullptr);
    esp_ota_img_states_t state = ESP_OTA_IMG_UNDEFINED;
    if (running != nullptr) {
        s.running_partition = running->label;
        s.running_version = esp_app_get_description()->version;
        s.running_state = esp_ota_get_state_partition(running, &state) == ESP_OK ? state_name(state) : "undefined";
    }
    if (other != nullptr) {
        s.other_partition = other->label;
        s.slot_size = other->size;
        esp_app_desc_t desc{};
        if (esp_ota_get_partition_description(other, &desc) == ESP_OK) s.other_version = desc.version;
        state = ESP_OTA_IMG_UNDEFINED;
        s.other_state = esp_ota_get_state_partition(other, &state) == ESP_OK ? state_name(state) : "undefined";
    }
    s.rollback_possible = esp_ota_check_rollback_is_possible();
    return s;
}

void OtaService::set_state(const char *state, const std::string &error)
{
    std::lock_guard<std::mutex> l(mutex_);
    st_.state = state;
    st_.error = error;
}

bool OtaService::upload_firmware(httpd_req_t *req, std::string &error)
{
    bool expected = false;
    if (!busy_.compare_exchange_strong(expected, true)) {
        error = "Another update is in progress";
        return false;
    }
    struct Release
    {
        std::atomic<bool> &b;
        ~Release() { b = false; }
    } release{busy_};

    if (pending_verify_ && !confirmed_) {
        error = "The running firmware has not passed its health check yet; try again in a minute";
        return false;
    }
    const esp_partition_t *target = esp_ota_get_next_update_partition(nullptr);
    if (target == nullptr) {
        error = "No OTA partition available";
        return false;
    }
    const std::size_t total = req->content_len;
    if (total < ota::kHeaderBytes) {
        error = "Upload is empty or too small";
        set_state("error", error);
        return false;
    }
    if (total > target->size) {
        error = "Image (" + std::to_string(total) + " bytes) does not fit the " + std::to_string(target->size) + "-byte slot";
        set_state("error", error);
        return false;
    }
    {
        std::lock_guard<std::mutex> l(mutex_);
        st_ = OtaStatus{};
        st_.state = "receiving";
        st_.total = static_cast<uint32_t>(total);
    }
    std::unique_ptr<uint8_t[]> buf(new (std::nothrow) uint8_t[kChunk]);
    if (!buf) {
        error = "Out of memory";
        set_state("error", error);
        return false;
    }

    // 1. Header: validated before the slot is erased.
    std::size_t got = 0;
    int timeouts = 0;
    while (got < ota::kHeaderBytes) {
        const int n = recv_some(req, buf.get() + got, ota::kHeaderBytes - got, timeouts);
        if (n <= 0) {
            error = "Upload interrupted";
            set_state("error", error);
            return false;
        }
        got += static_cast<std::size_t>(n);
    }
    ota::ImageInfo info;
    if (!ota::check_header(buf.get(), got, CONFIG_IDF_FIRMWARE_CHIP_ID, kProject, info, error)) {
        set_state("error", error);
        return false;
    }
    {
        std::lock_guard<std::mutex> l(mutex_);
        st_.new_version = info.version;
    }
    ESP_LOGI(TAG, "receiving firmware %s (%u bytes) into %s", info.version.c_str(), static_cast<unsigned>(total),
             target->label);

    // 2. Stream into the inactive slot (erased progressively).
    esp_ota_handle_t h = 0;
    esp_err_t err = esp_ota_begin(target, OTA_WITH_SEQUENTIAL_WRITES, &h);
    if (err != ESP_OK) {
        error = std::string("Could not start the update: ") + esp_err_to_name(err);
        set_state("error", error);
        return false;
    }
    std::size_t written = 0;
    std::size_t pending = got;
    while (true) {
        if (pending > 0) {
            err = esp_ota_write(h, buf.get(), pending);
            if (err != ESP_OK) {
                esp_ota_abort(h);
                error = std::string("Flash write failed: ") + esp_err_to_name(err);
                set_state("error", error);
                return false;
            }
            written += pending;
            pending = 0;
            std::lock_guard<std::mutex> l(mutex_);
            st_.received = static_cast<uint32_t>(written);
        }
        if (written >= total) break;
        const std::size_t want = std::min(kChunk, total - written);
        const int n = recv_some(req, buf.get(), want, timeouts);
        if (n <= 0) {
            esp_ota_abort(h);
            error = "Upload interrupted after " + std::to_string(written) + " of " + std::to_string(total) +
                    " bytes; the current firmware is unchanged";
            set_state("error", error);
            ESP_LOGW(TAG, "%s", error.c_str());
            return false;
        }
        pending = static_cast<std::size_t>(n);
    }

    // 3. Full verification, then (and only then) select it for boot.
    set_state("verifying");
    err = esp_ota_end(h);
    if (err != ESP_OK) {
        error = err == ESP_ERR_OTA_VALIDATE_FAILED ? "The image is corrupted (checksum/hash mismatch); nothing was changed"
                                                   : std::string("Image verification failed: ") + esp_err_to_name(err);
        set_state("error", error);
        ESP_LOGW(TAG, "%s", error.c_str());
        return false;
    }
    err = esp_ota_set_boot_partition(target);
    if (err != ESP_OK) {
        error = std::string("Could not select the new firmware: ") + esp_err_to_name(err);
        set_state("error", error);
        return false;
    }
    ESP_LOGI(TAG, "firmware %s verified and selected; restarting", info.version.c_str());
    set_state("rebooting");
    return true;
}

bool OtaService::upload_ui(httpd_req_t *req, const std::string &pack_file, std::string &error)
{
    bool expected = false;
    if (!busy_.compare_exchange_strong(expected, true)) {
        error = "Another update is in progress";
        return false;
    }
    struct Release
    {
        std::atomic<bool> &b;
        ~Release() { b = false; }
    } release{busy_};

    const std::size_t total = req->content_len;
    if (total < 12 || total > kMaxUiPack) {
        error = "A web UI pack is 1 byte to 600 KB";
        return false;
    }
    FsUsage fs;
    if (platform_fs_usage(fs) != ESP_OK) {
        error = "Filesystem unavailable";
        return false;
    }
    struct stat old_st{};
    const std::size_t old_size = stat(pack_file.c_str(), &old_st) == 0 ? static_cast<std::size_t>(old_st.st_size) : 0;
    const std::size_t free_now = fs.total > fs.used ? fs.total - fs.used : 0;
    // Prefer keeping the old pack until the new one is complete; if both do
    // not fit, the old one is removed first (the /recovery page remains).
    const bool keep_old = free_now >= total + kFsMargin;
    if (!keep_old && free_now + old_size < total + kFsMargin) {
        error = "Not enough storage for this UI pack (" + std::to_string(total / 1024) + " KB)";
        return false;
    }
    {
        std::lock_guard<std::mutex> l(mutex_);
        st_ = OtaStatus{};
        st_.state = "receiving";
        st_.total = static_cast<uint32_t>(total);
        st_.new_version = "web UI";
    }
    std::unique_ptr<uint8_t[]> buf(new (std::nothrow) uint8_t[kChunk]);
    if (!buf) {
        error = "Out of memory";
        set_state("error", error);
        return false;
    }
    int timeouts = 0;
    // Check the magic before touching the current pack.
    std::size_t got = 0;
    while (got < 4) {
        const int n = recv_some(req, buf.get() + got, 4 - got, timeouts);
        if (n <= 0) {
            error = "Upload interrupted";
            set_state("error", error);
            return false;
        }
        got += static_cast<std::size_t>(n);
    }
    if (std::memcmp(buf.get(), "LWPK", 4) != 0) {
        error = "Not a Lumen web UI pack (web.pack from npm run build:fs)";
        set_state("error", error);
        return false;
    }
    if (!keep_old) unlink(pack_file.c_str());
    const std::string tmp = pack_file + ".new";
    FILE *f = std::fopen(tmp.c_str(), "wb");
    if (f == nullptr) {
        error = "Could not create the file";
        set_state("error", error);
        return false;
    }
    std::size_t written = 0;
    std::size_t pending = got;
    bool ok = true;
    while (ok) {
        if (pending > 0) {
            ok = std::fwrite(buf.get(), 1, pending, f) == pending;
            written += pending;
            pending = 0;
            std::lock_guard<std::mutex> l(mutex_);
            st_.received = static_cast<uint32_t>(written);
        }
        if (!ok || written >= total) break;
        const int n = recv_some(req, buf.get(), std::min(kChunk, total - written), timeouts);
        if (n <= 0) {
            ok = false;
            break;
        }
        pending = static_cast<std::size_t>(n);
    }
    ok = std::fclose(f) == 0 && ok;
    std::size_t count = 0;
    if (!ok || !WebPack::validate(tmp, count)) {
        unlink(tmp.c_str());
        error = ok ? "The UI pack is corrupted; nothing was installed" : "Upload interrupted";
        if (!keep_old) error += " (open /recovery to upload it again)";
        set_state("error", error);
        return false;
    }
    if (rename(tmp.c_str(), pack_file.c_str()) != 0) {
        unlink(pack_file.c_str());
        if (rename(tmp.c_str(), pack_file.c_str()) != 0) {
            error = "Could not install the UI pack";
            set_state("error", error);
            return false;
        }
    }
    ESP_LOGI(TAG, "web UI installed: %u assets, %u bytes", static_cast<unsigned>(count), static_cast<unsigned>(written));
    set_state("idle");
    return true;
}

void OtaService::tick(uint32_t uptime_ms, bool healthy)
{
    if (!pending_verify_ || confirmed_) return;
#if defined(LUMEN_OTA_SELFTEST) && LUMEN_OTA_SELFTEST == 1
    healthy = false; // test image: never becomes healthy -> rollback after kFailAfterMs
#elif defined(LUMEN_OTA_SELFTEST) && LUMEN_OTA_SELFTEST == 2
    if (uptime_ms > 8000) abort(); // test image: crashes -> bootloader rollback
#endif
    if (healthy && uptime_ms >= kConfirmAfterMs) {
        const esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
        if (err == ESP_OK) {
            confirmed_ = true;
            ESP_LOGI(TAG, "firmware %s passed its health check and is now permanent", esp_app_get_description()->version);
        } else {
            ESP_LOGE(TAG, "could not confirm firmware: %s", esp_err_to_name(err));
        }
        return;
    }
    if (uptime_ms >= kFailAfterMs) {
        ESP_LOGE(TAG, "firmware failed its health check; rolling back to the previous version");
        esp_ota_mark_app_invalid_rollback_and_reboot();
    }
}

} // namespace lumen
