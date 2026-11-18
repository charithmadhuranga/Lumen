// Lumen HAL — addressable LED driver (SPEC §5.1, §7.5).
//
// RMT-based WS2812/WS2812B/SK6812 support. Updates run independently of the
// web server and networking.
#pragma once

#include "i_driver.h"

#include <cstdint>

namespace lumen
{

struct AddressableLedPixel
{
    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;
    uint8_t w = 0; // used for RGBW
};

enum class LedColorOrder : uint8_t
{
    kRgb = 0,
    kRgbw,
    kGrb,
    kGrbw,
};

struct AddressableLedConfig
{
    uint16_t led_count = 0;
    LedColorOrder color_order = LedColorOrder::kRgb;
    uint8_t brightness = 255; // global brightness 0..255
};

class IAddressableLedDriver : public IDriver
{
  public:
    // Configure count, color order, global brightness.
    virtual esp_err_t configure(const AddressableLedConfig &config) = 0;

    // Set a single pixel (index validated by driver).
    virtual esp_err_t set_pixel(uint16_t index, const AddressableLedPixel &pixel) = 0;

    // Fill all pixels with one color.
    virtual esp_err_t fill(const AddressableLedPixel &pixel) = 0;

    // Push buffered pixels to the strip (RMT transmit).
    virtual esp_err_t show() = 0;

    virtual esp_err_t set_brightness(uint8_t brightness) = 0;
    virtual esp_err_t get_brightness(uint8_t &brightness) = 0;
};

} // namespace lumen
