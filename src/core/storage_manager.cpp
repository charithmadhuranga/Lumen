// Lumen storage manager — host implementation backed by POSIX stdio
// (PosixFs). The ESP-IDF / LittleFS target implementation lives in
// src/core/platform/esp_littlefs.cpp and is excluded from the native build
// by platformio.ini's build_src_filter.
//
// Both implement IStorage (include/lumen/core/storage.hpp).
//
// Transactional write:
//   back up previous generation (.bak) -> write .tmp -> optional validate ->
//   atomic rename. Never destroys the last good copy.
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <sys/stat.h>

#include "lumen/core/storage.hpp"

namespace lumen
{

namespace
{

std::string backup_path(const std::string &path)
{
    return path + ".bak";
}

std::string tmp_path(const std::string &path)
{
    return path + ".tmp";
}

// Read an entire file into `out`. Returns false on any failure (missing,
// unreadable, empty).
bool read_file(const std::string &path, std::string &out)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        return false;
    }
    const std::streamsize size = file.tellg();
    if (size <= 0) {
        return false;
    }
    file.seekg(0, std::ios::beg);
    out.resize(static_cast<std::size_t>(size));
    file.read(out.data(), size);
    return file.gcount() == size;
}

bool write_file(const std::string &path, const std::string &contents)
{
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file.is_open()) {
        return false;
    }
    file.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    return static_cast<bool>(file);
}

bool file_exists(const std::string &path)
{
    struct stat st{};
    return stat(path.c_str(), &st) == 0;
}

bool copy_file(const std::string &src, const std::string &dst)
{
    std::string contents;
    if (!read_file(src, contents)) {
        return false;
    }
    return write_file(dst, contents);
}

// Create every parent directory of `path` (recursive mkdir). No-op when the
// parent already exists. Returns false only on a real mkdir failure.
bool ensure_parent_dir(const std::string &path)
{
    const size_t slash = path.find_last_of('/');
    if (slash == std::string::npos || slash == 0) {
        return true; // no parent, or parent is the filesystem root
    }
    const std::string dir = path.substr(0, slash);

    std::string acc;
    // Walk the path, mkdir each component that doesn't exist yet.
    size_t start = 0;
    while (start < dir.size()) {
        const size_t next = dir.find('/', start);
        const std::string part =
            (next == std::string::npos) ? dir.substr(start)
                                        : dir.substr(start, next - start);
        if (!part.empty()) {
            acc += "/";
            acc += part;
            struct stat st{};
            if (stat(acc.c_str(), &st) != 0) {
                if (mkdir(acc.c_str(), 0755) != 0) {
                    return false;
                }
            }
        }
        start = (next == std::string::npos) ? dir.size() : next + 1;
    }
    return true;
}

} // namespace

class PosixFs final : public IStorage
{
  public:
    // All paths passed to IStorage methods are relative to `root`.
    explicit PosixFs(std::string root) : root_(std::move(root))
    {
        if (!root_.empty() && root_.back() != '/') {
            root_ += '/';
        }
    }

    StorageError read(const std::string &path, std::string &out) override
    {
        const std::string full = root_ + path;
        if (read_file(full, out) && !out.empty()) {
            return StorageError::kOk;
        }
        // Fall back to the previous-generation backup.
        const std::string bak = root_ + backup_path(path);
        if (read_file(bak, out) && !out.empty()) {
            return StorageError::kOk;
        }
        return file_exists(full) ? StorageError::kCorrupt
                                 : StorageError::kNotFound;
    }

    StorageError write(const std::string &path,
                       const std::string &contents,
                       StorageValidator validator) override
    {
        const std::string full = root_ + path;
        const std::string bak = root_ + backup_path(path);
        const std::string tmp = root_ + tmp_path(path);

        // 1. Back up the previous generation, if any.
        if (file_exists(full) && !copy_file(full, bak)) {
            return StorageError::kIoError;
        }

        // Ensure parent directories exist (e.g. "config/lights.json").
        if (!ensure_parent_dir(tmp)) {
            std::remove(tmp.c_str());
            return StorageError::kIoError;
        }

        // 2. Write to the temp file.
        if (!write_file(tmp, contents)) {
            std::remove(tmp.c_str());
            return StorageError::kIoError;
        }

        // 3. Validate before committing.
        if (validator && !validator(contents)) {
            std::remove(tmp.c_str());
            return StorageError::kInvalid;
        }

        // 4. Atomic commit.
        if (std::rename(tmp.c_str(), full.c_str()) != 0) {
            std::remove(tmp.c_str());
            return StorageError::kIoError;
        }
        return StorageError::kOk;
    }

    StorageError remove(const std::string &path) override
    {
        const std::string full = root_ + path;
        const std::string bak = root_ + backup_path(path);
        bool had_file = file_exists(full);
        bool had_bak = file_exists(bak);
        if (!had_file && !had_bak) {
            return StorageError::kNotFound;
        }
        if (had_file && std::remove(full.c_str()) != 0) {
            return StorageError::kIoError;
        }
        if (had_bak && std::remove(bak.c_str()) != 0) {
            return StorageError::kIoError;
        }
        return StorageError::kOk;
    }

    bool exists(const std::string &path) override
    {
        return file_exists(root_ + path);
    }

    std::vector<std::string> list(const std::string &dir) override
    {
        // POSIX directory listing is intentionally minimal here; callers that
        // need directory enumeration (config manager registry) implement it
        // on top of read()/exists() per document. Kept for interface
        // completeness.
        (void)dir;
        return {};
    }

  private:
    std::string root_;
};

// Factory so callers don't name the concrete class directly.
IStorage *create_posix_fs(std::string root)
{
    return new PosixFs(std::move(root));
}

} // namespace lumen
