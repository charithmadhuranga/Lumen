// Lumen — board profile in-memory representation.
//
// Parsed from `boards/*.json` (validated against
// `schemas/board-profile.schema.json`). This is the structure the
// PinValidator and hardware manager consume.
#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace lumen
{

// A board-level resource (GPIO, PWM, I2C, UART, RMT, Ethernet).
struct BoardResource
{
    std::string id;                // e.g. "gpio_0", "i2c0", "ledc0"
    std::string type;              // gpio, pwm, i2c, uart, rmt, ethernet
    int32_t gpio = -1;             // -1 = not a pin resource
    std::string direction;         // input, output, bidirectional
    bool inverted = false;
    std::string pull;              // none, up, down
    bool safe_state_set = false;
    bool safe_state = false;       // for outputs

    // Named bus pins for multi-pin resources, e.g. i2c {"sda":4,"scl":16},
    // uart {"tx":14,"rx":33,"de":-1}, ethernet {"mdc":23,"mdio":18,
    // "clk":17,"power":-1}, rmt {"data":27}. Every entry >= 0 is a GPIO
    // claimed by this resource and goes through the same PinValidator
    // checks as `gpio` (AGENTS.md §4.4).
    std::vector<std::pair<std::string, int32_t>> pins;
    int32_t port = -1;             // peripheral port (I2C/UART controller)
    int32_t frequency = 0;         // bus/timer frequency in Hz (0 = default)

    // Returns the named pin, or -1 when absent.
    int32_t pin(const std::string &name) const
    {
        for (const auto &p : pins) {
            if (p.first == name) {
                return p.second;
            }
        }
        return -1;
    }
};

// A peripheral device on a bus/resource.
struct BoardPeripheral
{
    std::string id;                // e.g. "pca9685_0", "relays"
    std::string type;              // pca9685, pcf8574, relay, ethernet,
                                   // rs485, addressable_led
    std::string bus;               // resource id it sits on (may be empty)
    int32_t address = -1;          // I2C address (for pca9685, pcf8574)
    int32_t channels = 0;
    int32_t frequency = 0;
    bool active_low = false;       // inputs (pcf8574) / relays: inverted logic
    std::string settings_json;     // raw "settings" object (driver-specific)
    // unverified flags: the profile ships user-supplied values that need
    // physical verification (AGENTS.md §6). Never silently "fixed".
    bool unverified = false;
    bool address_verification_required = false;
    std::string unverified_note;
};

// Full board profile.
struct BoardProfile
{
    int32_t schema_version = 0;
    std::string id;
    std::string name;
    std::string manufacturer;
    std::string mcu;               // esp32, esp32s3, ...
    std::string description;
    std::vector<BoardResource> resources;
    std::vector<BoardPeripheral> peripherals;
    // unverified at top level means some capability claims are unverified.
    bool unverified = false;
};

// Index resources by id for quick lookup.
class BoardProfileIndex
{
  public:
    void index(const BoardProfile &profile)
    {
        profile_ = profile;
        resource_by_id_.clear();
        peripheral_by_id_.clear();
        for (std::size_t i = 0; i < profile_.resources.size(); ++i) {
            resource_by_id_.emplace(profile_.resources[i].id, i);
        }
        for (std::size_t i = 0; i < profile_.peripherals.size(); ++i) {
            peripheral_by_id_.emplace(profile_.peripherals[i].id, i);
        }
    }

    const BoardResource *find_resource(const std::string &id) const
    {
        auto it = resource_by_id_.find(id);
        if (it == resource_by_id_.end()) {
            return nullptr;
        }
        return &profile_.resources[it->second];
    }

    const BoardPeripheral *find_peripheral(const std::string &id) const
    {
        auto it = peripheral_by_id_.find(id);
        if (it == peripheral_by_id_.end()) {
            return nullptr;
        }
        return &profile_.peripherals[it->second];
    }

    // All resources using a given GPIO pin.
    std::vector<const BoardResource *> resources_on_gpio(int32_t gpio) const
    {
        std::vector<const BoardResource *> out;
        for (const auto &res : profile_.resources) {
            if (res.gpio == gpio) {
                out.push_back(&res);
            }
        }
        return out;
    }

    // All peripherals on a given bus resource id.
    std::vector<const BoardPeripheral *> peripherals_on_bus(
        const std::string &bus_id) const
    {
        std::vector<const BoardPeripheral *> out;
        for (const auto &periph : profile_.peripherals) {
            if (periph.bus == bus_id) {
                out.push_back(&periph);
            }
        }
        return out;
    }

    const BoardProfile &profile() const { return profile_; }

  private:
    BoardProfile profile_;
    std::unordered_map<std::string, std::size_t> resource_by_id_;
    std::unordered_map<std::string, std::size_t> peripheral_by_id_;
};

} // namespace lumen
