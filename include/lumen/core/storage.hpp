// Lumen storage interface — transactional file storage for JSON config
// documents (SPEC §19). Implemented on host by PosixFs and on target by
// EspLittleFs (src/core/platform/), both behind this mockable interface.
//
// Transactional write contract:
//   1. back up the previous generation (path -> path.bak)
//   2. write contents to path.tmp
//   3. optional validator rejects -> remove tmp, keep original + backup
//   4. atomic rename path.tmp -> path
//
// Never heap-allocates in hot paths beyond the caller-supplied string.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace lumen
{

enum class StorageError : uint8_t
{
    kOk = 0,
    kNotFound,   // file (and backup) missing
    kCorrupt,    // file present but unusable (caller may apply defaults)
    kIoError,    // underlying filesystem failure
    kInvalid,    // validator rejected the contents
    kNoSpace,    // not enough room
};

// Optional validation callback used by write(). Null = no validation.
using StorageValidator = bool (*)(const std::string &contents);

class IStorage
{
  public:
    virtual ~IStorage() = default;

    // Read a document's contents into `out`. Returns kNotFound when neither
    // the file nor its backup exists; kCorrupt when both are present but
    // unreadable/empty.
    virtual StorageError read(const std::string &path, std::string &out) = 0;

    // Transactional write with one-generation backup (see contract above).
    virtual StorageError write(const std::string &path,
                               const std::string &contents,
                               StorageValidator validator = nullptr) = 0;

    // Remove a document and its backup. Returns kNotFound if neither exists.
    virtual StorageError remove(const std::string &path) = 0;

    virtual bool exists(const std::string &path) = 0;

    // List entries (files/dirs) directly under `dir` (non-recursive).
    virtual std::vector<std::string> list(const std::string &dir) = 0;
};

IStorage *create_posix_fs(std::string root);

} // namespace lumen
