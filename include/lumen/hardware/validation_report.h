// Lumen — validation report.
//
// Structured error/warning list returned by the PinValidator and board
// profile loader. Each entry carries a stable reason code, the affected pin
// (when applicable), and a human-readable message. The Phase-7 UI renders
// these inline (SPEC §5.3).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace lumen
{

// Stable reason codes. Do not renumber or reuse — the UI and tests depend
// on them.
enum class ValidationReason : uint16_t
{
    kOk = 0,

    // --- pin-level ---
    kGpioDoesNotExist,       // pin number not valid for this MCU
    kInputOnlyAsOutput,      // input-only GPIO used as output
    kStrappingPinMisused,    // boot-strapping pin (0/2/12/15 ESP32) assigned
    kFlashReservedPin,       // flash/PSRAM reserved pin assigned
    kBoardReservedPin,       // board-specific reserved pin assigned
    kDuplicateAssignment,    // one pin assigned to two peripherals
    kDirectionMismatch,      // pin direction incompatible with peripheral
    kPeripheralUnresolved,   // peripheral reference not declared in profile

    // --- bus-level ---
    kI2cConflict,            // two peripherals on same I2C bus/address
    kUartConflict,           // two peripherals on same UART
    kEthernetConflict,       // Ethernet pins conflict with another peripheral

    // --- resource limits ---
    kPwmChannelOverflow,     // more PWM channels than LEDC provides
    kRmtResourceOverflow,    // more RMT channels than provided
    kPwmTimerOverflow,       // more PWM timers than provided

    // --- profile-level ---
    kProfileInvalidJson,
    kProfileSchemaViolation,
    kProfileUnknownMcu,
    kProfileMissingRequired,
};

struct ValidationEntry
{
    ValidationReason reason = ValidationReason::kOk;
    // Pin id / peripheral id / resource id this entry concerns (empty when
    // profile-level).
    std::string id;
    std::string message;
    bool is_error = true;
};

struct ValidationReport
{
    std::vector<ValidationEntry> errors;
    std::vector<ValidationEntry> warnings;

    bool has_errors() const { return !errors.empty(); }
    std::size_t error_count() const { return errors.size(); }
    std::size_t warning_count() const { return warnings.size(); }
};

} // namespace lumen
