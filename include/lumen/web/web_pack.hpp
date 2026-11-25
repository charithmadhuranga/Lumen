// Lumen — packed web UI (see webui/scripts/stage_fs.mjs for the format).
// One LittleFS file holds every gzipped asset; the index is loaded into RAM
// (~40 entries) and assets are streamed by offset.
#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace lumen
{

class WebPack
{
  public:
    struct Entry
    {
        std::string path; // no leading '/'
        uint32_t offset = 0;
        uint32_t length = 0;
    };

    explicit WebPack(std::string file) : file_(std::move(file)) {}

    // (Re)load the index. Returns false when missing/corrupt (UI absent).
    bool load();
    bool loaded() const { return loaded_; }
    std::size_t count() const { return entries_.size(); }
    std::size_t total_bytes() const { return total_; }

    // Finds an entry ("index.html", "assets/x.js").
    bool find(const std::string &path, Entry &out) const;
    const std::string &file() const { return file_; }

    // Validates a candidate pack file (used by the UI-update upload).
    static bool validate(const std::string &file, std::size_t &count);

  private:
    std::string file_;
    std::vector<Entry> entries_;
    std::size_t total_ = 0;
    bool loaded_ = false;
    mutable std::mutex mutex_;
};

} // namespace lumen
