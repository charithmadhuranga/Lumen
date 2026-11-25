// Lumen — packed web UI reader. See web_pack.hpp
#include "lumen/web/web_pack.hpp"

#include <cstdio>
#include <cstring>

namespace lumen
{

namespace
{
constexpr uint32_t kMaxEntries = 256;

bool read_u32(FILE *f, uint32_t &v)
{
    uint8_t b[4];
    if (std::fread(b, 1, 4, f) != 4) return false;
    v = static_cast<uint32_t>(b[0]) | (static_cast<uint32_t>(b[1]) << 8) | (static_cast<uint32_t>(b[2]) << 16) |
        (static_cast<uint32_t>(b[3]) << 24);
    return true;
}

bool parse(const std::string &file, std::vector<WebPack::Entry> &out, std::size_t &total)
{
    FILE *f = std::fopen(file.c_str(), "rb");
    if (f == nullptr) return false;
    std::fseek(f, 0, SEEK_END);
    const long size = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    char magic[4];
    uint32_t version = 0, count = 0;
    bool ok = size > 12 && std::fread(magic, 1, 4, f) == 4 && std::memcmp(magic, "LWPK", 4) == 0 &&
              read_u32(f, version) && version == 1 && read_u32(f, count) && count > 0 && count <= kMaxEntries;
    out.clear();
    total = 0;
    for (uint32_t i = 0; ok && i < count; ++i) {
        uint8_t lb[2];
        if (std::fread(lb, 1, 2, f) != 2) {
            ok = false;
            break;
        }
        const uint16_t len = static_cast<uint16_t>(lb[0] | (lb[1] << 8));
        if (len == 0 || len > 200) {
            ok = false;
            break;
        }
        WebPack::Entry e;
        e.path.resize(len);
        ok = std::fread(&e.path[0], 1, len, f) == len && read_u32(f, e.offset) && read_u32(f, e.length) &&
             e.path.find("..") == std::string::npos &&
             static_cast<long>(e.offset) + static_cast<long>(e.length) <= size;
        if (ok) {
            total += e.length;
            out.push_back(std::move(e));
        }
    }
    std::fclose(f);
    if (ok) {
        bool has_index = false;
        for (const auto &e : out) has_index |= e.path == "index.html";
        ok = has_index;
    }
    return ok;
}
} // namespace

bool WebPack::load()
{
    std::vector<Entry> entries;
    std::size_t total = 0;
    const bool ok = parse(file_, entries, total);
    std::lock_guard<std::mutex> lock(mutex_);
    loaded_ = ok;
    entries_ = ok ? std::move(entries) : std::vector<Entry>{};
    total_ = ok ? total : 0;
    return ok;
}

bool WebPack::find(const std::string &path, Entry &out) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto &e : entries_) {
        if (e.path == path) {
            out = e;
            return true;
        }
    }
    return false;
}

bool WebPack::validate(const std::string &file, std::size_t &count)
{
    std::vector<Entry> entries;
    std::size_t total = 0;
    const bool ok = parse(file, entries, total);
    count = entries.size();
    return ok;
}

} // namespace lumen
