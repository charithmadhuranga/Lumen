// Lumen — board profile service (SPEC §6.1, §5.4).
//
// Built-in profiles are compiled into the firmware (read-only, always
// available even on a blank filesystem); custom profiles live in storage
// under boards/<id>.json. The active profile id is persisted in
// board.json. Apply-with-rollback is orchestrated by the controller, which
// owns the hardware manager and the lighting engine.
#pragma once

#include <string>
#include <vector>

#include "lumen/core/config_manager.hpp"
#include "lumen/core/storage.hpp"
#include "lumen/hardware/board_profile.h"
#include "lumen/hardware/validation_report.h"

namespace lumen
{

struct BuiltinBoard
{
    const char *id;
    const char *json;
};

// Provided by the platform (embedded JSON on target, files on host).
const std::vector<BuiltinBoard> &builtin_boards();

struct BoardSummary
{
    std::string id;
    std::string name;
    std::string mcu;
    std::string description;
    bool builtin = false;
    bool active = false;
    bool unverified = false;
};

enum class BoardOpResult : uint8_t
{
    kOk = 0,
    kNotFound,
    kInvalid,     // parse/validation errors (see report)
    kReadOnly,    // built-in profiles cannot be modified/deleted
    kConflict,    // id exists / active profile cannot be deleted
    kIoError,
};

class BoardService
{
  public:
    static constexpr const char *kDefaultBoard = "kincony_kc868_ap";
    static constexpr std::size_t kMaxCustom = 16;

    BoardService(IStorage &storage, ConfigManager &config);

    // Reads board.json; falls back to the default built-in profile.
    void init();

    std::vector<BoardSummary> list() const;
    bool is_builtin(const std::string &id) const;
    bool get_json(const std::string &id, std::string &out) const;
    bool load(const std::string &id, BoardProfile &out) const;

    const std::string &active_id() const { return active_id_; }
    BoardOpResult set_active(const std::string &id);

    // Parse + semantic validation (PinValidator). `parse_ok` false means
    // the JSON itself was unusable.
    ValidationReport validate(const std::string &json, BoardProfile &parsed, bool &parse_ok) const;

    BoardOpResult create(const std::string &json, ValidationReport &report, std::string &id_out);
    BoardOpResult update(const std::string &id, const std::string &json, ValidationReport &report);
    BoardOpResult remove(const std::string &id);

  private:
    std::string custom_path(const std::string &id) const { return "boards/" + id + ".json"; }

    IStorage &storage_;
    ConfigManager &config_;
    std::string active_id_ = kDefaultBoard;
};

} // namespace lumen
