// Partition table validation — parses partitions.csv and enforces the
// Phase 0 contract (dual OTA + LittleFS data on 4 MB flash, alignment,
// no overlaps). Guards AGENTS.md §15 (changing the partition table).
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <unistd.h>
#include <vector>

#include <unity.h>

#ifndef LUMEN_REPO_ROOT
#define LUMEN_REPO_ROOT "."
#endif

namespace
{

struct Partition {
    std::string name;
    std::string type;
    std::string subtype;
    uint32_t offset = 0;
    uint32_t size = 0;
};

bool trim(const std::string &in, std::string &out)
{
    const auto begin = in.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) {
        out.clear();
        return false;
    }
    const auto end = in.find_last_not_of(" \t\r\n");
    out = in.substr(begin, end - begin + 1);
    return true;
}

std::vector<std::string> split_csv(const std::string &line)
{
    std::vector<std::string> parts;
    std::stringstream ss(line);
    std::string cell;
    while (std::getline(ss, cell, ',')) {
        std::string t;
        trim(cell, t);
        parts.push_back(t);
    }
    return parts;
}

bool parse_hex(const std::string &s, uint32_t &out)
{
    if (s.empty()) {
        return false;
    }
    const char *start = s.c_str();
    if (s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        start += 2;
    }
    char *end = nullptr;
    const unsigned long v = std::strtoul(start, &end, 16);
    if (end == start || *end != '\0') {
        return false;
    }
    out = static_cast<uint32_t>(v);
    return true;
}

std::vector<Partition> partitions; // loaded in test_table_parses()
bool load_failed = false;          // distinguishes "missing" from "empty"
std::string load_error;            // why loading failed, for diagnostics

// Reads LUMEN_REPO_ROOT/partitions.csv (falls back to ./) into `out`.
bool load_partitions(std::vector<Partition> &out)
{
    const std::string candidates[] = {
        std::string(LUMEN_REPO_ROOT) + "/partitions.csv",
        "partitions.csv",
        "../partitions.csv",
    };
    std::ifstream file;
    for (const auto &path : candidates) {
        file.open(path);
        if (file.is_open()) {
            break;
        }
        file.clear();
    }
    if (!file.is_open()) {
        load_error = "could not open partitions.csv";
        return false;
    }

    std::string line;
    int line_no = 0;
    while (std::getline(file, line)) {
        ++line_no;
        std::string t;
        // Comment/header lines: ';'-style (ESP-IDF) and '#'-style headers.
        if (!trim(line, t) || t[0] == ';' || t[0] == '#') {
            continue;
        }
        const auto cells = split_csv(t);
        if (cells.size() < 5) {
            load_error = "malformed line " + std::to_string(line_no);
            return false;
        }
        Partition p;
        p.name = cells[0];
        p.type = cells[1];
        p.subtype = cells[2];
        if (!parse_hex(cells[3], p.offset) || !parse_hex(cells[4], p.size)) {
            load_error = "bad hex offset/size on line " + std::to_string(line_no);
            return false;
        }
        out.push_back(p);
    }
    return !out.empty();
}

const Partition *find(const std::vector<Partition> &all, const std::string &name)
{
    for (const auto &p : all) {
        if (p.name == name) {
            return &p;
        }
    }
    return nullptr;
}

} // namespace

extern "C" void setUp(void) {}
extern "C" void tearDown(void) {}

static void test_table_parses(void)
{
    load_failed = !load_partitions(partitions);
    if (load_failed) {
        char msg[256];
        char cwd[160] = "?";
        (void)!getcwd(cwd, sizeof(cwd));
        std::snprintf(msg, sizeof(msg), "%s; LUMEN_REPO_ROOT=\"%s\", cwd=%s",
                      load_error.c_str(), LUMEN_REPO_ROOT, cwd);
        TEST_FAIL_MESSAGE(msg);
    }
    TEST_ASSERT_FALSE_MESSAGE(partitions.empty(), "partitions.csv is empty");
}

static void test_required_partitions_exist(void)
{
    const char *required[] = {"nvs", "otadata", "phy_init", "ota_0", "ota_1", "storage"};
    for (const char *name : required) {
        TEST_ASSERT_NOT_NULL_MESSAGE(find(partitions, name), name);
    }
}

static void test_offsets_aligned_and_non_overlapping(void)
{
    // Reorder by offset, then check 4 KB alignment and no overlap.
    std::vector<Partition> sorted = partitions;
    std::sort(sorted.begin(), sorted.end(),
              [](const Partition &a, const Partition &b) { return a.offset < b.offset; });

    uint32_t prev_end = 0;
    for (const auto &p : sorted) {
        TEST_ASSERT_EQUAL_HEX32_MESSAGE(0, p.offset % 0x1000, "offset must be 4 KB aligned");
        TEST_ASSERT_EQUAL_HEX32_MESSAGE(0, p.size % 0x1000, "size must be 4 KB aligned");
        TEST_ASSERT_TRUE_MESSAGE(p.size > 0, "size must be positive");
        TEST_ASSERT_TRUE_MESSAGE(p.offset >= prev_end, "partitions must not overlap");
        prev_end = p.offset + p.size;
    }
}

static void test_dual_ota_slots_are_equal(void)
{
    const Partition *ota0 = find(partitions, "ota_0");
    const Partition *ota1 = find(partitions, "ota_1");
    TEST_ASSERT_NOT_NULL(ota0);
    TEST_ASSERT_NOT_NULL(ota1);
    TEST_ASSERT_EQUAL_STRING("app", ota0->type.c_str());
    TEST_ASSERT_EQUAL_STRING("app", ota1->type.c_str());
    TEST_ASSERT_EQUAL_STRING("ota_0", ota0->subtype.c_str());
    TEST_ASSERT_EQUAL_STRING("ota_1", ota1->subtype.c_str());
    TEST_ASSERT_EQUAL_UINT32(ota0->size, ota1->size);
    TEST_ASSERT_TRUE_MESSAGE(ota0->size >= 0x80000, "OTA slot below 512 KB");
}

static void test_storage_fits_web_budget(void)
{
    // DESIGN.md §14.1 web bundle budget is <= 1.0 MB; LittleFS data
    // partition must be able to hold it (Phase 0 plan: >= 1 MB).
    const Partition *storage = find(partitions, "storage");
    TEST_ASSERT_NOT_NULL(storage);
    TEST_ASSERT_TRUE_MESSAGE(storage->size >= 0x100000, "storage partition below 1 MB");
}

static void test_nvs_minimum_size(void)
{
    const Partition *nvs = find(partitions, "nvs");
    TEST_ASSERT_NOT_NULL(nvs);
    TEST_ASSERT_TRUE_MESSAGE(nvs->size >= 0x4000, "nvs below 16 KB");
}

static void test_table_fits_4mb_flash(void)
{
    uint32_t highest = 0;
    for (const auto &p : partitions) {
        const uint32_t end = p.offset + p.size;
        if (end > highest) {
            highest = end;
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(highest <= 0x400000, "partition table exceeds 4 MB flash");
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    UNITY_BEGIN();
    RUN_TEST(test_table_parses);
    // If the table could not be read, the remaining tests would dereference
    // null lookups — stop the suite (failures stay visible in the count).
    if (!load_failed && !partitions.empty()) {
        RUN_TEST(test_required_partitions_exist);
        RUN_TEST(test_offsets_aligned_and_non_overlapping);
        RUN_TEST(test_dual_ota_slots_are_equal);
        RUN_TEST(test_storage_fits_web_budget);
        RUN_TEST(test_nvs_minimum_size);
        RUN_TEST(test_table_fits_4mb_flash);
    }
    return UNITY_END();
}
