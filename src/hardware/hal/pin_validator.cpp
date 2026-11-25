// Lumen — PinValidator implementation. See pin_validator.h for the contract.
#include "lumen/hardware/pin_validator.h"

#include <algorithm>
#include <cstring>
#include <sstream>

namespace lumen
{

PinValidator::PinValidator(const McuCapabilities &capabilities,
                           std::vector<int32_t> board_reserved)
    : capabilities_(capabilities), board_reserved_(std::move(board_reserved))
{
}

ValidationReport PinValidator::validate(const BoardProfileIndex &profile) const
{
    ValidationReport report;

    const BoardProfile &p = profile.profile();

    // MCU must be known.
    if (!McuCapabilityRegistry::known(p.mcu)) {
        report.errors.push_back(
            {ValidationReason::kProfileUnknownMcu, p.mcu,
             "MCU '" + p.mcu + "' is not in the capability table", true});
        return report;
    }

    validate_gpios(profile, report);
    validate_duplicates(profile, report);
    validate_peripheral_references(profile, report);
    validate_bus_conflicts(profile, report);
    validate_resource_limits(profile, report);

    return report;
}

namespace
{
bool pin_role_outputish(const std::string &type, const std::string &role)
{
    if (type == "i2c") return true;  // open-drain SDA/SCL
    if (role == "rx") return false;  // UART receive
    return true;                     // tx, de, mdc, mdio, clk, data, power
}
} // namespace

std::vector<PinAssignment> collect_pin_assignments(const BoardProfile &p)
{
    std::vector<PinAssignment> uses;
    for (const auto &res : p.resources) {
        if (res.gpio >= 0) {
            const bool outputish =
                (res.direction == "output" || res.direction == "bidirectional" ||
                 res.type == "pwm" || res.type == "rmt");
            uses.push_back({res.gpio, res.id, res.id + " (" + res.type + ")", res.type, outputish});
        }
        for (const auto &pin : res.pins) {
            if (pin.second >= 0) {
                uses.push_back({pin.second, res.id, res.id + "." + pin.first, pin.first,
                                pin_role_outputish(res.type, pin.first)});
            }
        }
    }
    return uses;
}

void PinValidator::validate_gpios(const BoardProfileIndex &profile,
                                  ValidationReport &report) const
{
    const BoardProfile &p = profile.profile();
    for (const auto &use : collect_pin_assignments(p)) {
        const std::string &id = use.label;
        const std::string g = std::to_string(use.gpio);
        if (!gpio_exists(use.gpio)) {
            report.errors.push_back({ValidationReason::kGpioDoesNotExist, id,
                                     "GPIO " + g + " does not exist on " + capabilities_.mcu,
                                     true});
            continue;
        }
        if (use.outputish && is_input_only(use.gpio)) {
            report.errors.push_back({ValidationReason::kInputOnlyAsOutput, id,
                                     "GPIO " + g + " is input-only on " + capabilities_.mcu,
                                     true});
        }
        if (is_strapping(use.gpio)) {
            report.warnings.push_back(
                {ValidationReason::kStrappingPinMisused, id,
                 "GPIO " + g + " is a boot-strapping pin on " + capabilities_.mcu +
                     "; changing it may prevent boot",
                 false});
        }
        {
            const auto &c = capabilities_.gpio.console;
            if (std::find(c.begin(), c.end(), static_cast<uint32_t>(use.gpio)) != c.end()) {
                report.warnings.push_back(
                    {ValidationReason::kBoardReservedPin, id,
                     "GPIO " + g + " is the serial console (UART0); using it disables logs and USB flashing",
                     false});
            }
        }
        if (is_flash_reserved(use.gpio)) {
            report.errors.push_back({ValidationReason::kFlashReservedPin, id,
                                     "GPIO " + g + " is reserved for flash/PSRAM on " +
                                         capabilities_.mcu,
                                     true});
        }
        if (is_board_reserved(use.gpio)) {
            report.errors.push_back({ValidationReason::kBoardReservedPin, id,
                                     "GPIO " + g + " is board-reserved and cannot be assigned",
                                     true});
        }
    }
}

void PinValidator::validate_duplicates(const BoardProfileIndex &profile,
                                       ValidationReport &report) const
{
    std::vector<PinAssignment> used = collect_pin_assignments(profile.profile());
    std::stable_sort(used.begin(), used.end(),
                     [](const PinAssignment &a, const PinAssignment &b) { return a.gpio < b.gpio; });
    for (std::size_t i = 1; i < used.size(); ++i) {
        if (used[i].gpio == used[i - 1].gpio) {
            report.errors.push_back(
                {ValidationReason::kDuplicateAssignment,
                 used[i - 1].owner + "+" + used[i].owner,
                 "GPIO " + std::to_string(used[i].gpio) + " assigned to both '" +
                     used[i - 1].label + "' and '" + used[i].label + "'",
                 true});
        }
    }
}

void PinValidator::validate_peripheral_references(
    const BoardProfileIndex &profile, ValidationReport &report) const
{
    const BoardProfile &p = profile.profile();

    for (const auto &periph : p.peripherals) {
        if (periph.bus.empty()) {
            continue; // no bus reference
        }
        if (!profile.find_resource(periph.bus)) {
            report.errors.push_back(
                {ValidationReason::kPeripheralUnresolved, periph.id,
                 "peripheral '" + periph.id + "' references undeclared bus '" +
                     periph.bus + "'",
                 true});
        }
    }
}

void PinValidator::validate_bus_conflicts(const BoardProfileIndex &profile,
                                          ValidationReport &report) const
{
    const BoardProfile &p = profile.profile();

    // I2C conflicts: two peripherals on the same I2C bus with the same
    // address.
    for (std::size_t i = 0; i < p.peripherals.size(); ++i) {
        for (std::size_t j = i + 1; j < p.peripherals.size(); ++j) {
            const auto &a = p.peripherals[i];
            const auto &b = p.peripherals[j];
            if (a.bus.empty() || a.bus != b.bus) {
                continue;
            }
            // Same bus. Check address conflict only when both have addresses.
            if (a.address >= 0 && b.address >= 0 && a.address == b.address &&
                (a.type == "pca9685" || a.type == "pcf8574" ||
                 b.type == "pca9685" || b.type == "pcf8574")) {
                report.errors.push_back(
                    {ValidationReason::kI2cConflict, a.id + "+" + b.id,
                     "I2C address conflict on bus '" + a.bus +
                         "': '" + a.id + "' and '" + b.id +
                         "' both at address " +
                         std::to_string(a.address),
                     true});
            }
        }
    }

    // UART conflicts: two peripherals using the same UART resource id.
    // (Handled by duplicate GPIO check on tx/rx pins; here we flag same-bus
    // serial peripherals.)
    for (std::size_t i = 0; i < p.peripherals.size(); ++i) {
        for (std::size_t j = i + 1; j < p.peripherals.size(); ++j) {
            const auto &a = p.peripherals[i];
            const auto &b = p.peripherals[j];
            if (a.bus.empty() || a.bus != b.bus) {
                continue;
            }
            const auto *ares = profile.find_resource(a.bus);
            const auto *bres = profile.find_resource(b.bus);
            if (ares && bres && ares->type == "uart" &&
                ares->type == bres->type) {
                report.warnings.push_back(
                    {ValidationReason::kUartConflict, a.id + "+" + b.id,
                     "two peripherals share UART '" + a.bus +
                         "'; ensure they use disjoint pins", false});
            }
        }
    }

    // Ethernet conflicts: two peripherals on the same ethernet resource.
    for (std::size_t i = 0; i < p.peripherals.size(); ++i) {
        for (std::size_t j = i + 1; j < p.peripherals.size(); ++j) {
            const auto &a = p.peripherals[i];
            const auto &b = p.peripherals[j];
            if (a.bus.empty() || a.bus != b.bus) {
                continue;
            }
            const auto *ares = profile.find_resource(a.bus);
            const auto *bres = profile.find_resource(b.bus);
            if (ares && bres && ares->type == "ethernet") {
                report.errors.push_back(
                    {ValidationReason::kEthernetConflict, a.id + "+" + b.id,
                     "two peripherals share Ethernet resource '" + a.bus +
                         "'",
                     true});
            }
        }
    }
}

void PinValidator::validate_resource_limits(const BoardProfileIndex &profile,
                                            ValidationReport &report) const
{
    const BoardProfile &p = profile.profile();

    // Count PWM and LEDC resources.
    uint32_t pwm_channels = 0;
    uint32_t rmt_channels = 0;

    for (const auto &res : p.resources) {
        if (res.type == "pwm") {
            ++pwm_channels;
        }
        if (res.type == "rmt") {
            rmt_channels += static_cast<uint32_t>(res.gpio >= 0 ? 1 : 0);
        }
    }

    // Peripherals that consume PWM/RMT.
    for (const auto &periph : p.peripherals) {
        if (periph.type == "pca9685") {
            // external PCA9685 does not consume LEDC channels; skip.
        }
        if (periph.channels > 0 && periph.type == "addressable_led") {
            rmt_channels += static_cast<uint32_t>(periph.channels);
        }
    }

    if (pwm_channels > capabilities_.ledc.channel_count) {
        report.errors.push_back(
            {ValidationReason::kPwmChannelOverflow, "pwm",
             "profile requests " + std::to_string(pwm_channels) +
                 " PWM channels but " + capabilities_.mcu + " provides " +
                 std::to_string(capabilities_.ledc.channel_count),
             true});
    }

    if (rmt_channels > capabilities_.rmt.channel_count) {
        report.errors.push_back(
            {ValidationReason::kRmtResourceOverflow, "rmt",
             "profile requests " + std::to_string(rmt_channels) +
                 " RMT channels but " + capabilities_.mcu + " provides " +
                 std::to_string(capabilities_.rmt.channel_count),
             true});
    }
}

bool PinValidator::gpio_exists(int32_t gpio) const
{
    if (gpio < 0 || static_cast<uint32_t>(gpio) >= capabilities_.gpio.gpio_count) {
        return false;
    }
    const auto &m = capabilities_.gpio.missing;
    return std::find(m.begin(), m.end(), static_cast<uint32_t>(gpio)) == m.end();
}

bool PinValidator::is_input_only(int32_t gpio) const
{
    return std::find(capabilities_.gpio.input_only.begin(),
                     capabilities_.gpio.input_only.end(), gpio) !=
           capabilities_.gpio.input_only.end();
}

bool PinValidator::is_strapping(int32_t gpio) const
{
    return std::find(capabilities_.gpio.strapping.begin(),
                     capabilities_.gpio.strapping.end(), gpio) !=
           capabilities_.gpio.strapping.end();
}

bool PinValidator::is_flash_reserved(int32_t gpio) const
{
    return std::find(capabilities_.gpio.flash_reserved.begin(),
                     capabilities_.gpio.flash_reserved.end(), gpio) !=
           capabilities_.gpio.flash_reserved.end();
}

bool PinValidator::is_board_reserved(int32_t gpio) const
{
    return std::find(board_reserved_.begin(), board_reserved_.end(), gpio) !=
           board_reserved_.end();
}

} // namespace lumen
