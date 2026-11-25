// Lumen — OTA image pre-validation (SPEC §20). Pure functions, host-tested.
//
// Checked on the first bytes of an upload, before anything is written to
// flash: ESP image magic, target chip, app-descriptor magic and project
// name. esp_ota_end() then verifies the full image (segments, checksum,
// SHA-256) before it can ever be selected for boot.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace lumen::ota
{

// esp_image_header_t (24 B) + first esp_image_segment_header_t (8 B), then
// esp_app_desc_t. Bytes needed to read version and project name:
constexpr std::size_t kAppDescOffset = 24 + 8;
constexpr std::size_t kHeaderBytes = kAppDescOffset + 4 + 4 + 8 + 32 + 32; // through project_name
constexpr uint8_t kImageMagic = 0xE9;
constexpr uint32_t kAppDescMagic = 0xABCD5432;

struct ImageInfo
{
    uint16_t chip_id = 0;
    std::string version;
    std::string project;
};

// Returns false with a user-facing `error` when the header cannot be a Lumen
// firmware for this chip. `len` must be >= kHeaderBytes.
bool check_header(const uint8_t *data, std::size_t len, uint16_t expected_chip, const char *expected_project,
                  ImageInfo &info, std::string &error);

} // namespace lumen::ota
