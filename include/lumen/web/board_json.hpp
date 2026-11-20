// Lumen board-profile JSON + validation core (Phase 7).
//
// Pure, host-testable helpers shared by the firmware HTTP routes
// (`api_routes.cpp`) and the native test suite. No ESP-IDF / httpd types
// here — this file compiles for both the device and the host so the
// serialization + PinValidator behaviour is unit-tested without hardware.
#pragma once

#include <string>

#include "lumen/hardware/board_profile.h"
#include "lumen/hardware/board_profile_loader.h"
#include "lumen/hardware/mcu_capabilities.h"
#include "lumen/hardware/pin_validator.h"
#include "lumen/hardware/validation_report.h"

namespace lumen
{
namespace web
{

// Serialize a BoardProfile to the schema_version-1 JSON shape understood by
// `load_board_profile`. Round-trips with the loader.
std::string board_profile_to_json(const BoardProfile &profile);

// Serialize a ValidationReport to JSON:
//   { "errors":[{code,id,message,is_error}], "warnings":[...],
//     "has_errors":bool, "error_count":n, "warning_count":n }
std::string validation_report_to_json(const ValidationReport &report);

// Stable string code for a ValidationReason, matching the firmware enum order
// and the UI's expected codes (e.g. "kDuplicateAssignment").
const char *validation_reason_code(ValidationReason reason);

// Validate a profile given as JSON. Parses via the board-profile loader and
// runs the PinValidator for the profile's MCU. `parse_ok` is false when the
// JSON could not be parsed into a profile (then the report carries a
// profile-level error).
ValidationReport validate_profile_json(const std::string &json, bool &parse_ok);

} // namespace web
} // namespace lumen