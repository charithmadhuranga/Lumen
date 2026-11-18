// Lumen — MCU capability tables.
//
// Per-MCU data, not #ifdef soup (SPEC §5.3). Each table entry describes what
// a specific MCU can provide. "MCU capability does not mean the board
// exposes it" (AGENTS.md §6): the board profile lists what is actually
// broken out; this table only bounds what is possible.
#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace lumen
{

struct McuGpioInfo
{
    uint32_t gpio_count = 0;          // total GPIOs (0..count-1 valid)
    std::vector<uint32_t> input_only; // GPIOs that can only be inputs
    std::vector<uint32_t> strapping;  // boot-strapping pins (use with care)
    std::vector<uint32_t> flash_reserved; // flash/PSRAM reserved pins
};

struct McuLedcInfo
{
    uint32_t timer_count = 0;   // independent LEDC timers
    uint32_t channel_count = 0; // total LEDC channels
    uint32_t max_frequency_hz = 0;
    uint32_t max_resolution_bits = 0;
};

struct McuRmtInfo
{
    uint32_t channel_count = 0; // RMT channels (TX+RX combined)
    uint32_t max_tx_channels = 0;
};

struct McuI2cInfo
{
    uint32_t bus_count = 0; // I2C controllers
};

struct McuUartInfo
{
    uint32_t uart_count = 0; // UART controllers
};

struct McuCapabilities
{
    std::string mcu;        // e.g. "esp32", "esp32s3"
    McuGpioInfo gpio;
    McuLedcInfo ledc;
    McuRmtInfo rmt;
    McuI2cInfo i2c;
    McuUartInfo uart;
    bool has_ethernet = false;
    bool has_esp_now = false;
};

// ----- built-in tables (esp32, esp32s3) -----

inline const McuCapabilities kEsp32Capabilities{
    "esp32",
    {/* gpio */ 40, /* input_only */ {34, 35, 36, 39},
     /* strapping */ {0, 2, 12, 15},
     /* flash_reserved */ {6, 7, 8, 9, 10, 11}},
    {/* ledc */ 8, /* timer */ 16, /* channel */ 80'000000, /* max res */ 20},
    {/* rmt */ 8, /* tx */ 8},
    {/* i2c */ 2},
    {/* uart */ 3},
    true, // ethernet
    true  // esp_now
};

inline const McuCapabilities kEsp32S3Capabilities{
    "esp32s3",
    {/* gpio */ 46, /* input_only */ {},
     /* strapping */ {0, 3, 4, 5},
     /* flash_reserved */ {0, 1, 2, 3, 4, 5, 45, 46}},
    {/* ledc */ 8, /* timer */ 16, /* channel */ 80'000000, /* max res */ 20},
    {/* rmt */ 8, /* tx */ 8},
    {/* i2c */ 2},
    {/* uart */ 3},
    true, // ethernet (not on all S3 variants, but capability exists)
    true  // esp_now
};

// Registry of all known MCU capabilities. Look up by mcu string.
class McuCapabilityRegistry
{
  public:
    // Returns nullptr when the MCU is unknown.
    static const McuCapabilities *find(const std::string &mcu)
    {
        if (mcu == "esp32") {
            return &kEsp32Capabilities;
        }
        if (mcu == "esp32s3") {
            return &kEsp32S3Capabilities;
        }
        return nullptr;
    }

    static bool known(const std::string &mcu)
    {
        return find(mcu) != nullptr;
    }

  private:
    McuCapabilityRegistry() = delete;
};

} // namespace lumen
