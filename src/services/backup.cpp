#include "lumen/services/backup.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include "core/cJSON.h"

namespace lumen::backup
{

namespace
{

bool is_dir(const std::string &p)
{
    struct stat st{};
    return stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

bool exists(const std::string &p)
{
    struct stat st{};
    return stat(p.c_str(), &st) == 0;
}

void remove_tree(const std::string &path)
{
    DIR *d = opendir(path.c_str());
    if (d == nullptr) {
        unlink(path.c_str());
        return;
    }
    while (struct dirent *e = readdir(d)) {
        const std::string name = e->d_name;
        if (name == "." || name == "..") continue;
        const std::string child = path + "/" + name;
        if (is_dir(child)) remove_tree(child);
        else unlink(child.c_str());
    }
    closedir(d);
    rmdir(path.c_str());
}

bool read_file(const std::string &p, std::string &out)
{
    FILE *f = std::fopen(p.c_str(), "rb");
    if (f == nullptr) return false;
    char buf[1024];
    out.clear();
    std::size_t n;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) out.append(buf, n);
    std::fclose(f);
    return true;
}

bool write_file(const std::string &p, const std::string &data)
{
    FILE *f = std::fopen(p.c_str(), "wb");
    if (f == nullptr) return false;
    const bool ok = std::fwrite(data.data(), 1, data.size(), f) == data.size();
    return std::fclose(f) == 0 && ok;
}

// Documents that belong to this controller and never travel in backups: the
// cloud pairing (its key is in NVS, which backups never contain). A full
// restore keeps the current copy (see restore_stream).
constexpr const char *kDeviceOnly[] = {"cloud.json"};

bool device_only(const std::string &rel)
{
    for (const char *d : kDeviceOnly) {
        if (rel == d) return true;
    }
    return false;
}

bool is_document(const std::string &name)
{
    // Storage keeps ".bak" / ".tmp" generations next to documents: never export them.
    return name.size() > 5 && name.compare(name.size() - 5, 5, ".json") == 0;
}

// Relative paths of every document under root (max depth 2).
void collect(const std::string &root, const std::string &rel, std::vector<std::string> &out, int depth)
{
    DIR *d = opendir((root + (rel.empty() ? "" : "/" + rel)).c_str());
    if (d == nullptr) return;
    while (struct dirent *e = readdir(d)) {
        const std::string name = e->d_name;
        if (name == "." || name == "..") continue;
        const std::string r = rel.empty() ? name : rel + "/" + name;
        if (is_dir(root + "/" + r)) {
            if (depth < 1) collect(root, r, out, depth + 1);
        } else if (is_document(name) && !device_only(r)) {
            out.push_back(r);
        }
    }
    closedir(d);
    std::sort(out.begin(), out.end());
}

bool copy_tree(const std::string &from, const std::string &to)
{
    std::vector<std::string> docs;
    collect(from, "", docs, 0);
    for (const auto &r : docs) {
        const std::size_t slash = r.find('/');
        if (slash != std::string::npos) mkdir((to + "/" + r.substr(0, slash)).c_str(), 0755);
        std::string data;
        if (!read_file(from + "/" + r, data) || !write_file(to + "/" + r, data)) return false;
    }
    return true;
}

std::string section_of(const std::string &rel)
{
    const std::size_t slash = rel.find('/');
    if (slash != std::string::npos) return rel.substr(0, slash);
    return rel.substr(0, rel.size() - 5); // strip ".json"
}

} // namespace

bool valid_document_path(const std::string &path)
{
    if (path.empty() || path.size() > 96 || !is_document(path) || path.front() == '/' || path.find("..") != std::string::npos) {
        return false;
    }
    int slashes = 0;
    for (char c : path) {
        if (c == '/') {
            ++slashes;
            continue;
        }
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' ||
                        c == '-' || c == '.';
        if (!ok) return false;
    }
    return slashes <= 1 && path.find("//") == std::string::npos && path.back() != '/';
}

bool write(const std::string &root, const std::string &firmware, const std::string &board, long long created,
           const std::vector<std::string> &sections, const Sink &sink)
{
    auto str = [](const std::string &v) {
        cJSON *s = cJSON_CreateString(v.c_str());
        char *t = cJSON_PrintUnformatted(s);
        std::string out = t ? t : "\"\"";
        cJSON_free(t);
        cJSON_Delete(s);
        return out;
    };
    std::string head = "{\"format\":\"lumen-backup\",\"version\":" + std::to_string(kFormatVersion) +
                       ",\"firmware\":" + str(firmware) + ",\"board\":" + str(board) +
                       ",\"created\":" + std::to_string(created) + ",\"documents\":{";
    if (!sink(head.data(), head.size())) return false;
    std::vector<std::string> files;
    collect(root, "", files, 0);
    bool first = true;
    for (const auto &r : files) {
        if (!sections.empty() && std::find(sections.begin(), sections.end(), section_of(r)) == sections.end()) continue;
        std::string text;
        if (!read_file(root + "/" + r, text) || text.size() > kMaxDocument) continue;
        cJSON *doc = cJSON_Parse(text.c_str()); // only well-formed documents are exported
        const bool ok = doc != nullptr && (cJSON_IsObject(doc) || cJSON_IsArray(doc));
        cJSON_Delete(doc);
        if (!ok) continue;
        const std::string key = (first ? "" : ",") + str(r) + ":";
        first = false;
        if (!sink(key.data(), key.size()) || !sink(text.data(), text.size())) return false;
    }
    return sink("}}", 2);
}

std::string create(const std::string &root, const std::string &firmware, const std::string &board, long long created,
                   const std::vector<std::string> &sections)
{
    std::string out;
    write(root, firmware, board, created, sections, [&out](const char *d, std::size_t n) {
        out.append(d, n);
        return true;
    });
    return out;
}

namespace
{

// Minimal streaming JSON reader: just enough to walk the top level of a
// backup and capture each document's raw text (validated with cJSON).
class Reader
{
  public:
    explicit Reader(const Source &src) : src_(src) {}

    int peek()
    {
        if (pos_ == len_) {
            if (eof_) return -1;
            const int n = src_(buf_, sizeof(buf_));
            if (n <= 0) {
                eof_ = true;
                if (n < 0) failed_ = true;
                return -1;
            }
            len_ = static_cast<std::size_t>(n);
            pos_ = 0;
            total_ += len_;
        }
        return static_cast<unsigned char>(buf_[pos_]);
    }
    int get()
    {
        const int c = peek();
        if (c >= 0) ++pos_;
        return c;
    }
    void ws()
    {
        while (peek() == ' ' || peek() == '\n' || peek() == '\r' || peek() == '\t') get();
    }
    bool expect(char c)
    {
        ws();
        return get() == c;
    }
    // Plain-ASCII string (keys / short values); escapes other than \" \\ \/ rejected.
    bool string(std::string &out, std::size_t max)
    {
        ws();
        if (get() != '"') return false;
        out.clear();
        for (;;) {
            int c = get();
            if (c < 0) return false;
            if (c == '"') return true;
            if (c == '\\') {
                c = get();
                if (c != '"' && c != '\\' && c != '/') return false;
            }
            if (c < 0x20 || out.size() >= max) return false;
            out.push_back(static_cast<char>(c));
        }
    }
    // Raw text of any JSON value (strings/escapes/nesting respected).
    bool value(std::string &out, std::size_t max)
    {
        ws();
        out.clear();
        int depth = 0;
        bool in_str = false, esc = false;
        for (;;) {
            const int c = peek();
            if (c < 0) return depth == 0 && !in_str && !out.empty();
            if (!in_str && depth == 0 && !out.empty() && (c == ',' || c == '}' || c == ']' || c == ' ' || c == '\n' || c == '\r' || c == '\t')) {
                return true;
            }
            get();
            if (out.size() >= max) return false;
            out.push_back(static_cast<char>(c));
            if (in_str) {
                if (esc) esc = false;
                else if (c == '\\') esc = true;
                else if (c == '"') {
                    in_str = false;
                    if (depth == 0) return true;
                }
            } else if (c == '"') {
                in_str = true;
            } else if (c == '{' || c == '[') {
                ++depth;
            } else if (c == '}' || c == ']') {
                if (--depth < 0) return false;
                if (depth == 0) return true;
            }
        }
    }
    bool failed() const { return failed_; }
    std::size_t total() const { return total_; }

  private:
    const Source &src_;
    char buf_[512];
    std::size_t pos_ = 0, len_ = 0, total_ = 0;
    bool eof_ = false, failed_ = false;
};

} // namespace

bool restore_stream(const std::string &root, const Source &source, bool replace_all, std::string &error,
                    std::vector<std::string> *restored)
{
    const std::string staging = root + ".restore";
    const std::string old = root + ".old";
    remove_tree(staging);
    remove_tree(old);
    auto fail = [&](const std::string &why) {
        remove_tree(staging);
        if (restored) restored->clear();
        error = why;
        return false;
    };
    if (mkdir(staging.c_str(), 0755) != 0) return fail("Could not create the staging area");
    if (!replace_all && !copy_tree(root, staging)) return fail("Could not copy the current configuration");

    Reader in(source);
    std::string key, text, format;
    double version = 0;
    std::size_t docs = 0;
    bool have_docs = false;
    if (!in.expect('{')) return fail("Not a Lumen backup file");
    in.ws();
    if (in.peek() == '}') return fail("Not a Lumen backup file");
    for (;;) {
        if (!in.string(key, 64) || !in.expect(':')) return fail("Not a Lumen backup file");
        if (key == "documents") {
            have_docs = true;
            if (!in.expect('{')) return fail("\"documents\" must be an object");
            in.ws();
            if (in.peek() == '}') {
                in.get();
            } else {
                for (;;) {
                    if (!in.string(key, 96) || !in.expect(':')) return fail("Malformed documents list");
                    if (!valid_document_path(key)) return fail("Invalid document name '" + key + "'");
                    if (device_only(key)) return fail("'" + key + "' belongs to one controller and cannot be restored");
                    if (++docs > kMaxDocuments) return fail("Too many documents in the backup");
                    if (!in.value(text, kMaxDocument)) return fail("Document '" + key + "' is malformed or larger than 32 KB");
                    cJSON *d = cJSON_Parse(text.c_str());
                    const bool ok = d != nullptr && (cJSON_IsObject(d) || cJSON_IsArray(d));
                    cJSON_Delete(d);
                    if (!ok) return fail("Document '" + key + "' is not a JSON object");
                    const std::size_t slash = key.find('/');
                    if (slash != std::string::npos) mkdir((staging + "/" + key.substr(0, slash)).c_str(), 0755);
                    if (!write_file(staging + "/" + key, text)) return fail("Storage full or write failed; the current configuration is unchanged");
                    if (restored) restored->push_back(key);
                    in.ws();
                    const int c = in.get();
                    if (c == '}') break;
                    if (c != ',') return fail("Malformed documents list");
                }
            }
        } else {
            if (!in.value(text, 256)) return fail("Malformed backup header");
            if (key == "format") {
                format = text.size() >= 2 && text.front() == '"' ? text.substr(1, text.size() - 2) : "";
            } else if (key == "version") {
                version = std::atof(text.c_str());
            }
        }
        if (in.total() > kMaxBytes) return fail("Backup is larger than 1 MB");
        in.ws();
        const int c = in.get();
        if (c == '}') break;
        if (c != ',') return fail("Not a Lumen backup file");
    }
    in.ws();
    if (in.failed()) return fail("Upload interrupted; the current configuration is unchanged");
    if (in.peek() >= 0) return fail("Unexpected data after the backup");
    if (format != "lumen-backup" || !have_docs) return fail("Not a Lumen backup file");
    if (version < 1 || version > kFormatVersion) return fail("Backup format version is not supported by this firmware");
    if (docs == 0) return fail("The backup contains no documents");

    // A full restore replaces everything except this controller's own documents.
    if (replace_all) {
        for (const char *d : kDeviceOnly) {
            std::string keep;
            if (read_file(root + "/" + d, keep) && !write_file(staging + "/" + d, keep)) {
                return fail("Storage full or write failed; the current configuration is unchanged");
            }
        }
    }

    // Swap (two renames). recover_interrupted() handles a cut in between.
    if (rename(root.c_str(), old.c_str()) != 0) return fail("Could not replace the configuration");
    if (rename(staging.c_str(), root.c_str()) != 0) {
        rename(old.c_str(), root.c_str());
        return fail("Could not replace the configuration; the current one was kept");
    }
    remove_tree(old);
    return true;
}

bool restore(const std::string &root, const std::string &json, bool replace_all, std::string &error,
             std::vector<std::string> *restored)
{
    std::size_t pos = 0;
    return restore_stream(root, [&](char *buf, std::size_t len) {
        const std::size_t n = std::min(len, json.size() - pos);
        std::memcpy(buf, json.data() + pos, n);
        pos += n;
        return static_cast<int>(n);
    }, replace_all, error, restored);
}

bool recover_interrupted(const std::string &root)
{
    bool changed = false;
    const std::string old = root + ".old";
    const std::string staging = root + ".restore";
    if (!is_dir(root) && is_dir(old)) {
        changed = rename(old.c_str(), root.c_str()) == 0;
    }
    if (exists(staging)) {
        remove_tree(staging);
        changed = true;
    }
    if (is_dir(root) && exists(old)) {
        remove_tree(old);
        changed = true;
    }
    return changed;
}

} // namespace lumen::backup
