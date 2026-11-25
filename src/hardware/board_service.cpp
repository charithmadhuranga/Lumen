// Lumen — board profile service. See board_service.h
#include "lumen/hardware/board_service.h"

#include "core/cJSON.h"
#include "lumen/hardware/board_profile_loader.h"
#include "lumen/web/board_json.hpp"

namespace lumen
{

namespace
{
constexpr const char *kBoardDoc = "board.json";

bool valid_board_id(const std::string &id)
{
    if (id.empty() || id.size() > 40) return false;
    for (char c : id) {
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-')) {
            return false;
        }
    }
    return true;
}
} // namespace

BoardService::BoardService(IStorage &storage, ConfigManager &config)
    : storage_(storage), config_(config)
{
}

void BoardService::init()
{
    DocConfig doc{kBoardDoc, 1, migrate_additive, nullptr, nullptr};
    config_.register_document(doc);
    std::string contents;
    if (config_.load_document(kBoardDoc, contents) == ConfigStatus::kOk) {
        cJSON *root = cJSON_Parse(contents.c_str());
        const cJSON *active = root ? cJSON_GetObjectItemCaseSensitive(root, "active") : nullptr;
        if (cJSON_IsString(active) && active->valuestring != nullptr) {
            BoardProfile p;
            if (load(active->valuestring, p)) {
                active_id_ = active->valuestring;
            }
        }
        cJSON_Delete(root);
    }
}

bool BoardService::is_builtin(const std::string &id) const
{
    for (const auto &b : builtin_boards()) {
        if (id == b.id) return true;
    }
    return false;
}

bool BoardService::get_json(const std::string &id, std::string &out) const
{
    for (const auto &b : builtin_boards()) {
        if (id == b.id) {
            out = b.json;
            return true;
        }
    }
    if (!valid_board_id(id)) return false;
    return storage_.read(custom_path(id), out) == StorageError::kOk;
}

bool BoardService::load(const std::string &id, BoardProfile &out) const
{
    std::string json;
    return get_json(id, json) && load_board_profile(json, out);
}

std::vector<BoardSummary> BoardService::list() const
{
    std::vector<BoardSummary> out;
    auto add = [&](const std::string &json, bool builtin) {
        BoardProfile p;
        if (!load_board_profile(json, p)) return;
        for (const auto &existing : out) {
            if (existing.id == p.id) return;
        }
        BoardSummary s;
        s.id = p.id;
        s.name = p.name;
        s.mcu = p.mcu;
        s.description = p.description;
        s.builtin = builtin;
        s.active = (p.id == active_id_);
        s.unverified = p.unverified;
        out.push_back(s);
    };
    for (const auto &b : builtin_boards()) {
        add(b.json, true);
    }
    for (const auto &path : storage_.list("boards")) {
        if (path.size() < 6 || path.compare(path.size() - 5, 5, ".json") != 0) continue;
        std::string json;
        if (storage_.read(path, json) == StorageError::kOk) {
            add(json, false);
        }
    }
    return out;
}

BoardOpResult BoardService::set_active(const std::string &id)
{
    BoardProfile p;
    if (!load(id, p)) return BoardOpResult::kNotFound;
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "active", id.c_str());
    char *text = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    const ConfigStatus st = config_.save_document(kBoardDoc, text ? text : "{}");
    cJSON_free(text);
    if (st != ConfigStatus::kOk) return BoardOpResult::kIoError;
    active_id_ = id;
    return BoardOpResult::kOk;
}

ValidationReport BoardService::validate(const std::string &json, BoardProfile &parsed,
                                        bool &parse_ok) const
{
    ValidationReport report = web::validate_profile_json(json, parse_ok);
    if (parse_ok) {
        load_board_profile(json, parsed);
        if (!valid_board_id(parsed.id)) {
            report.errors.push_back({ValidationReason::kProfileSchemaViolation, parsed.id,
                                     "id must be 1-40 characters of a-z, 0-9, '_' or '-'", true});
        }
        // Peripheral id uniqueness + required fields per type.
        for (std::size_t i = 0; i < parsed.peripherals.size(); ++i) {
            const auto &p = parsed.peripherals[i];
            for (std::size_t j = i + 1; j < parsed.peripherals.size(); ++j) {
                if (parsed.peripherals[j].id == p.id) {
                    report.errors.push_back({ValidationReason::kDuplicateAssignment, p.id,
                                             "duplicate peripheral id '" + p.id + "'", true});
                }
            }
            if ((p.type == "pca9685" || p.type == "pcf8574") &&
                (p.address < 0x03 || p.address > 0x77)) {
                report.errors.push_back({ValidationReason::kProfileMissingRequired, p.id,
                                         "'" + p.id + "' needs a 7-bit I2C address (0x03-0x77)",
                                         true});
            }
            if (p.address_verification_required) {
                report.warnings.push_back({ValidationReason::kAddressUnverified, p.id,
                                           "'" + p.id +
                                               "' address is unverified - confirm it with an I2C scan",
                                           false});
            }
        }
        for (const auto &r : parsed.resources) {
            if (r.type == "i2c" && (r.pin("sda") < 0 || r.pin("scl") < 0)) {
                report.errors.push_back({ValidationReason::kProfileMissingRequired, r.id,
                                         "I2C bus '" + r.id + "' needs pins.sda and pins.scl", true});
            }
            if (r.type == "pwm" && r.gpio < 0) {
                report.errors.push_back({ValidationReason::kProfileMissingRequired, r.id,
                                         "PWM output '" + r.id + "' needs a gpio", true});
            }
        }
    }
    return report;
}

BoardOpResult BoardService::create(const std::string &json, ValidationReport &report,
                                   std::string &id_out)
{
    BoardProfile p;
    bool parse_ok = false;
    report = validate(json, p, parse_ok);
    if (!parse_ok || report.has_errors()) return BoardOpResult::kInvalid;
    if (is_builtin(p.id) || storage_.exists(custom_path(p.id))) return BoardOpResult::kConflict;
    std::size_t custom = 0;
    for (const auto &s : list()) custom += s.builtin ? 0 : 1;
    if (custom >= kMaxCustom) return BoardOpResult::kConflict;
    const std::string normalized = web::board_profile_to_json(p);
    if (storage_.write(custom_path(p.id), normalized) != StorageError::kOk) {
        return BoardOpResult::kIoError;
    }
    id_out = p.id;
    return BoardOpResult::kOk;
}

BoardOpResult BoardService::update(const std::string &id, const std::string &json,
                                   ValidationReport &report)
{
    if (is_builtin(id)) return BoardOpResult::kReadOnly;
    if (!valid_board_id(id) || !storage_.exists(custom_path(id))) return BoardOpResult::kNotFound;
    BoardProfile p;
    bool parse_ok = false;
    report = validate(json, p, parse_ok);
    if (!parse_ok || report.has_errors()) return BoardOpResult::kInvalid;
    if (p.id != id) {
        report.errors.push_back({ValidationReason::kProfileSchemaViolation, p.id,
                                 "profile id must match the URL", true});
        return BoardOpResult::kInvalid;
    }
    return storage_.write(custom_path(id), web::board_profile_to_json(p)) == StorageError::kOk
               ? BoardOpResult::kOk
               : BoardOpResult::kIoError;
}

BoardOpResult BoardService::remove(const std::string &id)
{
    if (is_builtin(id)) return BoardOpResult::kReadOnly;
    if (id == active_id_) return BoardOpResult::kConflict;
    if (!valid_board_id(id)) return BoardOpResult::kNotFound;
    const StorageError err = storage_.remove(custom_path(id));
    if (err == StorageError::kNotFound) return BoardOpResult::kNotFound;
    return err == StorageError::kOk ? BoardOpResult::kOk : BoardOpResult::kIoError;
}

} // namespace lumen
