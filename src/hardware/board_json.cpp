// Lumen board-profile JSON + validation core implementation (Phase 7).
// See include/lumen/web/board_json.hpp. Uses the vendored cJSON (core/cJSON.h).
#include "lumen/web/board_json.hpp"

#include <string>
#include <vector>

#include "core/cJSON.h"

namespace lumen
{
namespace web
{

namespace
{

void add_if_nonempty(cJSON *obj, const char *key, const std::string &value)
{
    if (!value.empty()) {
        cJSON_AddStringToObject(obj, key, value.c_str());
    }
}

} // namespace

std::string board_profile_to_json(const BoardProfile &profile)
{
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        return {};
    }

    cJSON_AddNumberToObject(root, "schema_version",
                            profile.schema_version > 0 ? profile.schema_version : 1);
    cJSON_AddStringToObject(root, "id", profile.id.c_str());
    add_if_nonempty(root, "name", profile.name);
    add_if_nonempty(root, "manufacturer", profile.manufacturer);
    cJSON_AddStringToObject(root, "mcu", profile.mcu.c_str());
    add_if_nonempty(root, "description", profile.description);

    cJSON *resources = cJSON_CreateArray();
    cJSON *peripherals = cJSON_CreateArray();
    if (!resources || !peripherals) {
        cJSON_Delete(resources);
        cJSON_Delete(peripherals);
        cJSON_Delete(root);
        return {};
    }
    cJSON_AddItemToObject(root, "resources", resources);
    cJSON_AddItemToObject(root, "peripherals", peripherals);

    for (const auto &r : profile.resources) {
        cJSON *item = cJSON_CreateObject();
        if (!item) continue;
        cJSON_AddStringToObject(item, "id", r.id.c_str());
        cJSON_AddStringToObject(item, "type", r.type.c_str());
        cJSON_AddNumberToObject(item, "gpio", r.gpio);
        cJSON_AddStringToObject(item, "direction", r.direction.c_str());
        if (r.inverted) cJSON_AddBoolToObject(item, "inverted", true);
        if (!r.pull.empty()) cJSON_AddStringToObject(item, "pull", r.pull.c_str());
        if (r.safe_state_set) cJSON_AddBoolToObject(item, "safe_state", r.safe_state);
        if (r.port >= 0) cJSON_AddNumberToObject(item, "port", r.port);
        if (r.frequency > 0) cJSON_AddNumberToObject(item, "frequency", r.frequency);
        if (!r.pins.empty()) {
            cJSON *pins = cJSON_AddObjectToObject(item, "pins");
            for (const auto &pin : r.pins) {
                cJSON_AddNumberToObject(pins, pin.first.c_str(), pin.second);
            }
        }
        cJSON_AddItemToArray(resources, item);
    }

    for (const auto &p : profile.peripherals) {
        cJSON *item = cJSON_CreateObject();
        if (!item) continue;
        cJSON_AddStringToObject(item, "id", p.id.c_str());
        cJSON_AddStringToObject(item, "type", p.type.c_str());
        if (!p.bus.empty()) cJSON_AddStringToObject(item, "bus", p.bus.c_str());
        cJSON_AddNumberToObject(item, "address", p.address);
        cJSON_AddNumberToObject(item, "channels", p.channels);
        if (p.frequency > 0) cJSON_AddNumberToObject(item, "frequency", p.frequency);
        if (p.unverified) cJSON_AddBoolToObject(item, "unverified", true);
        if (p.address_verification_required) {
            cJSON_AddBoolToObject(item, "address_verification_required", true);
        }
        add_if_nonempty(item, "unverified_note", p.unverified_note);
        if (p.active_low) cJSON_AddBoolToObject(item, "active_low", true);
        if (!p.settings_json.empty()) {
            cJSON *settings = cJSON_Parse(p.settings_json.c_str());
            if (settings != nullptr) cJSON_AddItemToObject(item, "settings", settings);
        }
        cJSON_AddItemToArray(peripherals, item);
    }

    if (profile.unverified) {
        cJSON_AddBoolToObject(root, "unverified", true);
    }

    char *printed = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!printed) {
        return {};
    }
    std::string out(printed);
    cJSON_free(printed);
    return out;
}

const char *validation_reason_code(ValidationReason reason)
{
    switch (reason) {
        case ValidationReason::kOk: return "kOk";
        case ValidationReason::kAddressUnverified: return "kAddressUnverified";
        case ValidationReason::kGpioDoesNotExist: return "kGpioDoesNotExist";
        case ValidationReason::kInputOnlyAsOutput: return "kInputOnlyAsOutput";
        case ValidationReason::kStrappingPinMisused: return "kStrappingPinMisused";
        case ValidationReason::kFlashReservedPin: return "kFlashReservedPin";
        case ValidationReason::kBoardReservedPin: return "kBoardReservedPin";
        case ValidationReason::kDuplicateAssignment: return "kDuplicateAssignment";
        case ValidationReason::kDirectionMismatch: return "kDirectionMismatch";
        case ValidationReason::kPeripheralUnresolved: return "kPeripheralUnresolved";
        case ValidationReason::kI2cConflict: return "kI2cConflict";
        case ValidationReason::kUartConflict: return "kUartConflict";
        case ValidationReason::kEthernetConflict: return "kEthernetConflict";
        case ValidationReason::kPwmChannelOverflow: return "kPwmChannelOverflow";
        case ValidationReason::kRmtResourceOverflow: return "kRmtResourceOverflow";
        case ValidationReason::kPwmTimerOverflow: return "kPwmTimerOverflow";
        case ValidationReason::kProfileInvalidJson: return "kProfileInvalidJson";
        case ValidationReason::kProfileSchemaViolation: return "kProfileSchemaViolation";
        case ValidationReason::kProfileUnknownMcu: return "kProfileUnknownMcu";
        case ValidationReason::kProfileMissingRequired: return "kProfileMissingRequired";
    }
    return "kOk";
}

std::string validation_report_to_json(const ValidationReport &report)
{
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        return {};
    }

    auto emit = [](cJSON *arr, const std::vector<ValidationEntry> &entries) {
        for (const auto &e : entries) {
            cJSON *item = cJSON_CreateObject();
            if (!item) continue;
            cJSON_AddStringToObject(item, "code", validation_reason_code(e.reason));
            cJSON_AddStringToObject(item, "id", e.id.c_str());
            cJSON_AddStringToObject(item, "message", e.message.c_str());
            cJSON_AddBoolToObject(item, "is_error", e.is_error);
            cJSON_AddItemToArray(arr, item);
        }
    };

    cJSON *errors = cJSON_CreateArray();
    cJSON *warnings = cJSON_CreateArray();
    if (!errors || !warnings) {
        cJSON_Delete(errors);
        cJSON_Delete(warnings);
        cJSON_Delete(root);
        return {};
    }
    emit(errors, report.errors);
    emit(warnings, report.warnings);
    cJSON_AddItemToObject(root, "errors", errors);
    cJSON_AddItemToObject(root, "warnings", warnings);
    cJSON_AddBoolToObject(root, "has_errors", report.has_errors());
    cJSON_AddNumberToObject(root, "error_count",
                            static_cast<double>(report.error_count()));
    cJSON_AddNumberToObject(root, "warning_count",
                            static_cast<double>(report.warning_count()));

    char *printed = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!printed) {
        return {};
    }
    std::string out(printed);
    cJSON_free(printed);
    return out;
}

ValidationReport validate_profile_json(const std::string &json, bool &parse_ok)
{
    parse_ok = false;
    ValidationReport report;

    BoardProfile profile;
    if (!load_board_profile(json, profile)) {
        parse_ok = false;
        ValidationEntry e;
        e.reason = ValidationReason::kProfileInvalidJson;
        e.message = "Profile JSON could not be parsed or is missing required fields";
        e.is_error = true;
        report.errors.push_back(e);
        return report;
    }
    parse_ok = true;

    const McuCapabilities *caps = McuCapabilityRegistry::find(profile.mcu);
    if (caps == nullptr) {
        ValidationEntry e;
        e.reason = ValidationReason::kProfileUnknownMcu;
        e.id = profile.mcu;
        e.message = "Unknown MCU: " + profile.mcu;
        e.is_error = true;
        report.errors.push_back(e);
        return report;
    }

    BoardProfileIndex index;
    index.index(profile);

    PinValidator validator(*caps);
    report = validator.validate(index);
    return report;
}

} // namespace web
} // namespace lumen