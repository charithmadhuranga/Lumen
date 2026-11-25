// Lumen — firmware OTA with rollback, and web UI updates (SPEC §20).
//
// Firmware: POST /ota/upload streams the image into the inactive OTA slot.
//   * header checked before any flash write (chip, Lumen app descriptor),
//     size checked against the slot;
//   * esp_ota_end() verifies the complete image (checksum + SHA-256); only a
//     verified image is selected for boot — a failed or interrupted upload
//     leaves the running firmware selected and untouched;
//   * the new firmware boots "pending verify" (bootloader rollback enabled)
//     and marks itself valid only after a health check (web server up,
//     lighting loop ticking, ≥ kConfirmAfterMs uptime). A crash or reset
//     before that makes the bootloader return to the previous firmware.
//   * configuration lives in LittleFS/NVS, which OTA never touches.
// Web UI: POST /system/ui/upload replaces web.pack (validated; the built-in
//   recovery page at /recovery stays available if the UI is missing).
#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>

#include "esp_err.h"
#include "esp_http_server.h"

namespace lumen
{

struct OtaStatus
{
    std::string state = "idle"; // idle | receiving | verifying | rebooting | error
    uint32_t received = 0;
    uint32_t total = 0;
    std::string error;
    std::string new_version;
    std::string running_partition;
    std::string running_version;
    std::string running_state; // valid | pending_verify | ...
    std::string other_partition;
    std::string other_version; // "" when the slot holds no valid image
    std::string other_state;
    uint32_t slot_size = 0;
    bool rollback_possible = false;
};

class OtaService
{
  public:
    static constexpr uint32_t kConfirmAfterMs = 20000;
    static constexpr uint32_t kFailAfterMs = 120000;

    void boot();
    OtaStatus status() const;

    // Streams the request body into the inactive slot (httpd task).
    // Returns true when the image is verified and selected for boot.
    bool upload_firmware(httpd_req_t *req, std::string &error);

    // Streams a web.pack into LittleFS and swaps it in. `reload` re-reads
    // the server's pack index afterwards.
    bool upload_ui(httpd_req_t *req, const std::string &pack_file, std::string &error);

    // Housekeeping: confirm a pending image once healthy, roll back if it
    // never becomes healthy.
    void tick(uint32_t uptime_ms, bool healthy);

    bool busy() const { return busy_.load(); }

  private:
    void set_state(const char *state, const std::string &error = "");

    mutable std::mutex mutex_;
    OtaStatus st_;
    std::atomic<bool> busy_{false};
    bool pending_verify_ = false;
    bool confirmed_ = false;
};

} // namespace lumen
