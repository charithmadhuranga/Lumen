// Host tests for the storage manager: transactional writes, one-generation
// backup, corrupt/missing fallback, validator rejection. Fault injection is
// done by corrupting files directly (simulating a torn write / power loss).
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include <unity.h>

#include "lumen/core/storage.hpp"

using lumen::IStorage;
using lumen::StorageError;
using lumen::StorageValidator;
using lumen::create_posix_fs;

namespace
{

std::string g_root;

extern "C" void setUp(void)
{
    g_root = std::string(LUMEN_REPO_ROOT) + "/.test_storage_tmp";
    // Start clean.
    std::string cmd = "rm -rf \"";
    cmd += g_root + "\" && mkdir -p \"";
    cmd += g_root + "\"";
    (void)std::system(cmd.c_str());
}

extern "C" void tearDown(void)
{
    std::string cmd = "rm -rf \"";
    cmd += g_root + "\"";
    (void)std::system(cmd.c_str());
}

// A validator that accepts only JSON-looking documents (starts with '{').
static bool json_only_validator(const std::string &contents)
{
    return !contents.empty() && contents[0] == '{';
}

} // namespace

// ---------------------------------------------------------------------------
static void test_write_then_read_round_trip(void)
{
    IStorage *fs = create_posix_fs(g_root);
    TEST_ASSERT_NOT_NULL(fs);

    StorageError err = fs->write("config/lights.json", "{\"power\":true}");
    TEST_ASSERT_EQUAL(StorageError::kOk, err);

    std::string out;
    err = fs->read("config/lights.json", out);
    TEST_ASSERT_EQUAL(StorageError::kOk, err);
    TEST_ASSERT_EQUAL_STRING("{\"power\":true}", out.c_str());

    delete fs;
}

static void test_missing_file_returns_not_found(void)
{
    IStorage *fs = create_posix_fs(g_root);

    std::string out;
    StorageError err = fs->read("config/missing.json", out);
    TEST_ASSERT_EQUAL(StorageError::kNotFound, err);

    delete fs;
}

static void test_validator_rejection_keeps_original(void)
{
    IStorage *fs = create_posix_fs(g_root);

    // Good document first.
    TEST_ASSERT_EQUAL(StorageError::kOk,
                      fs->write("config/lights.json", "{\"power\":true}"));

    // Invalid document rejected by validator.
    TEST_ASSERT_EQUAL(StorageError::kInvalid,
                      fs->write("config/lights.json", "not-json", json_only_validator));

    // Original intact.
    std::string out;
    TEST_ASSERT_EQUAL(StorageError::kOk, fs->read("config/lights.json", out));
    TEST_ASSERT_EQUAL_STRING("{\"power\":true}", out.c_str());

    // No temp file left behind.
    TEST_ASSERT_FALSE(fs->exists("config/lights.json.tmp"));

    delete fs;
}

static void test_backup_kept_on_successful_write(void)
{
    IStorage *fs = create_posix_fs(g_root);

    TEST_ASSERT_EQUAL(StorageError::kOk,
                      fs->write("config/lights.json", "{\"power\":true}"));
    TEST_ASSERT_EQUAL(StorageError::kOk,
                      fs->write("config/lights.json", "{\"power\":false}"));

    // Backup holds the previous generation.
    std::string bak;
    TEST_ASSERT_EQUAL(StorageError::kOk, fs->read("config/lights.json.bak", bak));
    TEST_ASSERT_EQUAL_STRING("{\"power\":true}", bak.c_str());

    delete fs;
}

static void test_corrupt_original_falls_back_to_backup(void)
{
    IStorage *fs = create_posix_fs(g_root);

    TEST_ASSERT_EQUAL(StorageError::kOk,
                      fs->write("config/lights.json", "{\"power\":true}"));
    TEST_ASSERT_EQUAL(StorageError::kOk,
                      fs->write("config/lights.json", "{\"power\":false}"));

    // Simulate a torn/zeroed live file (atomic rename keeps either the old
    // or new version; a crash can zero it). Storage falls back to the
    // previous-generation backup.
    std::string live = g_root + "/config/lights.json";
    FILE *f = std::fopen(live.c_str(), "wb");
    TEST_ASSERT_NOT_NULL(f);
    std::fclose(f); // truncate to zero bytes

    std::string out;
    StorageError err = fs->read("config/lights.json", out);
    TEST_ASSERT_EQUAL(StorageError::kOk, err);
    // Backup holds the previous generation (first write).
    TEST_ASSERT_EQUAL_STRING("{\"power\":true}", out.c_str());

    delete fs;
}

static void test_empty_file_without_backup_is_corrupt_not_ok(void)
{
    IStorage *fs = create_posix_fs(g_root);

    TEST_ASSERT_EQUAL(StorageError::kOk,
                      fs->write("config/lights.json", "{\"power\":true}"));

    // Empty the only copy — no backup exists for the first generation.
    std::string live = g_root + "/config/lights.json";
    FILE *f = std::fopen(live.c_str(), "wb");
    TEST_ASSERT_NOT_NULL(f);
    std::fclose(f);

    std::string out;
    StorageError err = fs->read("config/lights.json", out);
    TEST_ASSERT_EQUAL(StorageError::kCorrupt, err); // present but unusable

    delete fs;
}

static void test_remove_deletes_file_and_backup(void)
{
    IStorage *fs = create_posix_fs(g_root);

    TEST_ASSERT_EQUAL(StorageError::kOk,
                      fs->write("config/lights.json", "{\"power\":true}"));
    TEST_ASSERT_EQUAL(StorageError::kOk,
                      fs->write("config/lights.json", "{\"power\":false}"));

    TEST_ASSERT_TRUE(fs->exists("config/lights.json"));
    TEST_ASSERT_TRUE(fs->exists("config/lights.json.bak"));

    TEST_ASSERT_EQUAL(StorageError::kOk, fs->remove("config/lights.json"));

    TEST_ASSERT_FALSE(fs->exists("config/lights.json"));
    TEST_ASSERT_FALSE(fs->exists("config/lights.json.bak"));

    // Removing again returns kNotFound.
    TEST_ASSERT_EQUAL(StorageError::kNotFound, fs->remove("config/lights.json"));

    delete fs;
}

static void test_nested_directory_creates_ok(void)
{
    IStorage *fs = create_posix_fs(g_root);

    TEST_ASSERT_EQUAL(StorageError::kOk,
                      fs->write("rooms/living/lights.json", "{\"id\":\"a\"}"));

    std::string out;
    TEST_ASSERT_EQUAL(StorageError::kOk, fs->read("rooms/living/lights.json", out));
    TEST_ASSERT_EQUAL_STRING("{\"id\":\"a\"}", out.c_str());

    delete fs;
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_write_then_read_round_trip);
    RUN_TEST(test_missing_file_returns_not_found);
    RUN_TEST(test_validator_rejection_keeps_original);
    RUN_TEST(test_backup_kept_on_successful_write);
    RUN_TEST(test_corrupt_original_falls_back_to_backup);
    RUN_TEST(test_empty_file_without_backup_is_corrupt_not_ok);
    RUN_TEST(test_remove_deletes_file_and_backup);
    RUN_TEST(test_nested_directory_creates_ok);
    return UNITY_END();
}
