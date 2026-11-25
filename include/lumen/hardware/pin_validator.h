// Lumen — PinValidator.
//
// Semantic pin validation for board profiles (SPEC §5.3). Operates on a
// BoardProfileIndex against per-MCU capability tables (data-driven, no
// #ifdef soup). Returns a structured ValidationReport with stable reason
// codes.
//
// Rules enforced:
//   * GPIO exists for the MCU
//   * input-only GPIOs not used as outputs
//   * strapping pins flagged (warning, user justifies)
//   * flash/PSRAM reserved pins rejected
//   * board-reserved pins rejected
//   * no duplicate GPIO assignments
//   * peripheral bus references resolve to declared resources
//   * UART / I2C / Ethernet conflicts detected
//   * PWM channel / timer limits, RMT limits enforced
#pragma once

#include "board_profile.h"
#include "mcu_capabilities.h"
#include "validation_report.h"

#include <cstddef>
#include <string>
#include <vector>

namespace lumen
{

// One GPIO claimed by a profile resource: its `gpio` field or a named bus
// pin (e.g. i2c0.sda). Used by the validator and the Pin Mapping API.
struct PinAssignment
{
    int32_t gpio;
    std::string owner;   // resource id
    std::string label;   // "id (type)" or "id.sda"
    std::string role;    // resource type or bus pin name
    bool outputish;      // drives the pin
};

std::vector<PinAssignment> collect_pin_assignments(const BoardProfile &profile);

class PinValidator
{
  public:
    // Validate `profile` against `capabilities`. `board_reserved` are pins
    // the board itself reserves (e.g. onboard LED, download button) and are
    // rejected when assigned to a peripheral.
    explicit PinValidator(const McuCapabilities &capabilities,
                          std::vector<int32_t> board_reserved = {});

    ValidationReport validate(const BoardProfileIndex &profile) const;

  private:
    const McuCapabilities &capabilities_;
    std::vector<int32_t> board_reserved_;

    void validate_gpios(const BoardProfileIndex &profile,
                        ValidationReport &report) const;
    void validate_duplicates(const BoardProfileIndex &profile,
                             ValidationReport &report) const;
    void validate_peripheral_references(const BoardProfileIndex &profile,
                                        ValidationReport &report) const;
    void validate_bus_conflicts(const BoardProfileIndex &profile,
                                ValidationReport &report) const;
    void validate_resource_limits(const BoardProfileIndex &profile,
                                  ValidationReport &report) const;

    bool gpio_exists(int32_t gpio) const;
    bool is_input_only(int32_t gpio) const;
    bool is_strapping(int32_t gpio) const;
    bool is_flash_reserved(int32_t gpio) const;
    bool is_board_reserved(int32_t gpio) const;
};

} // namespace lumen
