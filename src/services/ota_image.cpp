#include "lumen/services/ota_image.h"

#include <cstring>

namespace lumen::ota
{

namespace
{

uint32_t le32(const uint8_t *p)
{
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

std::string cstr(const uint8_t *p, std::size_t max)
{
    std::size_t n = 0;
    while (n < max && p[n] != 0) ++n;
    std::string s(reinterpret_cast<const char *>(p), n);
    for (char &c : s) {
        if (static_cast<unsigned char>(c) < 0x20 || static_cast<unsigned char>(c) > 0x7e) c = '?';
    }
    return s;
}

const char *chip_name(uint16_t id)
{
    switch (id) {
    case 0: return "ESP32";
    case 2: return "ESP32-S2";
    case 5: return "ESP32-C3";
    case 9: return "ESP32-S3";
    case 12: return "ESP32-C2";
    case 13: return "ESP32-C6";
    case 16: return "ESP32-H2";
    case 18: return "ESP32-P4";
    default: return "an unknown chip";
    }
}

} // namespace

bool check_header(const uint8_t *data, std::size_t len, uint16_t expected_chip, const char *expected_project,
                  ImageInfo &info, std::string &error)
{
    if (data == nullptr || len < kHeaderBytes) {
        error = "File is too small to be a firmware image";
        return false;
    }
    if (data[0] != kImageMagic) {
        error = "Not an ESP32 firmware image (use firmware.bin, not a .elf or a filesystem image)";
        return false;
    }
    info.chip_id = static_cast<uint16_t>(data[12] | (data[13] << 8));
    if (info.chip_id != expected_chip) {
        error = std::string("This image is built for ") + chip_name(info.chip_id) + ", this controller is " +
                chip_name(expected_chip);
        return false;
    }
    const uint8_t *desc = data + kAppDescOffset;
    if (le32(desc) != kAppDescMagic) {
        error = "Firmware image has no application descriptor";
        return false;
    }
    info.version = cstr(desc + 16, 32);
    info.project = cstr(desc + 48, 32);
    if (expected_project != nullptr && info.project != expected_project) {
        error = "This is not a Lumen firmware (project '" + info.project + "')";
        return false;
    }
    if (info.version.empty()) {
        error = "Firmware image has no version";
        return false;
    }
    return true;
}

} // namespace lumen::ota
