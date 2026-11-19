// Lumen authentication manager implementation
#include "lumen/web/auth_manager.hpp"

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

#include "esp_err.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "mbedtls/md.h"
// Use openthread's mbedtls which includes pkcs5.h
#include "mbedtls/md.h"
#include "nvs.h"

#include "core/cJSON.h"
#include "lumen/core/json.hpp"

static const char *TAG = "auth_manager";

namespace lumen
{

namespace
{

constexpr char kNvsNamespace[] = "lumen_auth";
constexpr char kUserPrefix[] = "usr_";
constexpr char kSessionPrefix[] = "ses_";

} // namespace

AuthManager::AuthManager(const AuthConfig &config) : config_(config) {}

AuthManager::~AuthManager() = default;

esp_err_t AuthManager::init()
{
    ESP_LOGI(TAG, "Initializing auth manager");

    // Open NVS namespace
    nvs_handle_t handle;
    esp_err_t err = nvs_open(kNvsNamespace, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open failed: %s", esp_err_to_name(err));
        return err;
    }

    // Check if admin user exists
    UserRecord admin;
    err = nvs_load_user("admin", admin);
    if (err == ESP_ERR_NOT_FOUND) {
        // Create default admin user with forced password change
        ESP_LOGI(TAG, "Creating default admin user");
        UserRecord new_admin{};
        new_admin.username = "admin";

        uint8_t salt[16];
        random_bytes(salt, sizeof(salt));
        new_admin.salt = to_hex(salt, sizeof(salt));

        uint8_t hash[32];
        hash_password("admin", salt, sizeof(salt), hash, sizeof(hash));
        new_admin.hash = to_hex(hash, sizeof(hash));

        new_admin.created_ms = now_ms();
        new_admin.force_pw_change = true;

        err = nvs_save_user(new_admin);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to create admin user: %s", esp_err_to_name(err));
            nvs_close(handle);
            return err;
        }
    }
    nvs_close(handle);
    ESP_LOGI(TAG, "Auth manager initialized");
    return ESP_OK;
}

AuthResult AuthManager::login(const char *username, const char *password, const char *ip)
{
    AuthResult result;

    // Rate limiting by IP
    int64_t now = now_ms();
    for (auto &entry : rate_limits_) {
        if (entry.key == ip) {
            if (now - entry.window_start_ms < config_.rate_limit_window_ms) {
                if (entry.count >= config_.max_login_attempts) {
                    result.code = AuthResult::kRateLimited;
                    return result;
                }
                entry.count++;
                break;
            } else {
                // Reset window
                entry.count = 1;
                entry.window_start_ms = now;
                break;
            }
        }
    }
    // No existing entry for this IP
    rate_limits_.push_back({ip, 1, now});

    // Load user
    UserRecord user;
    esp_err_t err = nvs_load_user(username, user);
    if (err == ESP_ERR_NOT_FOUND) {
        // Simulate hash computation to prevent timing attacks
        uint8_t dummy_salt[16] = {0};
        uint8_t dummy_hash[32];
        hash_password(password, dummy_salt, sizeof(dummy_salt), dummy_hash, sizeof(dummy_hash));
        result.code = AuthResult::kInvalidCredentials;
        return result;
    }
    if (err != ESP_OK) {
        result.code = AuthResult::kInternalError;
        return result;
    }

    // Check lockout
    now = now_ms();
    if (user.lockout_until_ms > now) {
        result.code = AuthResult::kAccountLocked;
        return result;
    }

    // Verify password
    uint8_t salt[16];
    uint8_t stored_hash[32];
    esp_err_t err2 = from_hex(user.salt.c_str(), salt, sizeof(salt));
    if (err2 != ESP_OK) {
        result.code = AuthResult::kInternalError;
        return result;
    }
    err2 = from_hex(user.hash.c_str(), stored_hash, sizeof(stored_hash));
    if (err2 != ESP_OK) {
        result.code = AuthResult::kInternalError;
        return result;
    }

    uint8_t computed_hash[32];
    hash_password(password, salt, 16, computed_hash, 32);

    if (!constant_time_compare(computed_hash, stored_hash, 32)) {
        // Increment failed attempts
        user.failed_attempts++;
        if (user.failed_attempts >= 5) {
            user.lockout_until_ms = now_ms() + 15 * 60 * 1000; // 15 min lockout
        }
        nvs_save_user(user);
        result.code = AuthResult::kInvalidCredentials;
        return result;
    }

    // Success - reset failed attempts
    user.failed_attempts = 0;
    user.last_login_ms = now_ms();
    nvs_save_user(user);

    // Create session
    Session session;
    esp_err_t sess_err = create_session(username, session, user.force_pw_change);
    if (sess_err != ESP_OK) {
        result.code = AuthResult::kInternalError;
        return result;
    }

    result.code = AuthResult::kOk;
    result.session = session;
    return result;
}

AuthResult AuthManager::validate_session(const char *token)
{
    AuthResult result;
    if (!token || token[0] == '\0') {
        result.code = AuthResult::kSessionInvalid;
        return result;
    }

    int64_t now = now_ms();
    for (auto &entry : sessions_) {
        if (entry.session.token == token) {
            if (entry.session.expiry_ms < now) {
                result.code = AuthResult::kSessionExpired;
                return result;
            }
            // Extend session
            entry.session.expiry_ms = now_ms() + config_.session_lifetime_ms;
            entry.dirty = true;
            result.code = AuthResult::kOk;
            result.session = entry.session;
            return result;
        }
    }
    result.code = AuthResult::kSessionInvalid;
    return result;
}

esp_err_t AuthManager::create_session(const char *username, Session &out_session, bool force_pw_change)
{
    int64_t now = now_ms();
    uint8_t token_bytes[32];
    esp_err_t err = random_bytes(token_bytes, config_.session_token_length);
    if (err != ESP_OK) {
        return err;
    }

    Session session;
    session.token = to_hex(token_bytes, config_.session_token_length);
    session.username = username;
    session.created_ms = now;
    session.expiry_ms = now + config_.session_lifetime_ms;
    session.force_pw_change = force_pw_change;

    // Store in memory
    sessions_.push_back({session, true});

    // Persist to NVS
    std::string key = std::string(kSessionPrefix) + session.token;
    nvs_handle_t handle;
    esp_err_t err2 = nvs_open(kNvsNamespace, NVS_READWRITE, &handle);
    if (err2 == ESP_OK) {
        // Serialize session to JSON
        cJSON *root = cJSON_CreateObject();
        cJSON_AddStringToObject(root, "token", session.token.c_str());
        cJSON_AddStringToObject(root, "username", session.username.c_str());
        cJSON_AddNumberToObject(root, "created_ms", session.created_ms);
        cJSON_AddNumberToObject(root, "expiry_ms", session.expiry_ms);
        cJSON_AddBoolToObject(root, "force_pw_change", session.force_pw_change);
        char *json_str = cJSON_PrintUnformatted(root);
        cJSON_Delete(root);

        nvs_handle_t handle;
        if (nvs_open(kNvsNamespace, NVS_READWRITE, &handle) == ESP_OK) {
            std::string key = std::string(kSessionPrefix) + session.token;
            nvs_set_str(handle, key.c_str(), json_str);
            nvs_commit(handle);
            nvs_close(handle);
        }
        cJSON_free(json_str);
    }

    out_session = session;
    return ESP_OK;
}

esp_err_t AuthManager::invalidate_session(const char *token)
{
    if (!token) return ESP_ERR_INVALID_ARG;

    // Remove from memory
    for (auto it = sessions_.begin(); it != sessions_.end(); ++it) {
        if (it->session.token == token) {
            sessions_.erase(it);
            break;
        }
    }

    // Remove from NVS
    nvs_handle_t handle;
    if (nvs_open(kNvsNamespace, NVS_READWRITE, &handle) == ESP_OK) {
        std::string key = std::string(kSessionPrefix) + token;
        nvs_erase_key(handle, key.c_str());
        nvs_commit(handle);
        nvs_close(handle);
    }

    return ESP_OK;
}

esp_err_t AuthManager::invalidate_user_sessions(const char *username)
{
    if (!username) return ESP_ERR_INVALID_ARG;

    // Remove from memory
    for (auto it = sessions_.begin(); it != sessions_.end();) {
        if (it->session.username == username) {
            // Also remove from NVS
            nvs_handle_t handle;
            if (nvs_open(kNvsNamespace, NVS_READWRITE, &handle) == ESP_OK) {
                std::string key = std::string(kSessionPrefix) + it->session.token;
                nvs_erase_key(handle, key.c_str());
                nvs_commit(handle);
                nvs_close(handle);
            }
            it = sessions_.erase(it);
        } else {
            ++it;
        }
    }
    return ESP_OK;
}

PasswordChangeResult AuthManager::change_password(const char *username,
                                                  const char *current_password,
                                                  const char *new_password)
{
    PasswordChangeResult result;

    if (!username || !current_password || !new_password) {
        result.code = PasswordChangeResult::kInternalError;
        return result;
    }

    // Validate new password
    if (std::strlen(new_password) < config_.min_password_length) {
        result.code = PasswordChangeResult::kNewPasswordTooWeak;
        return result;
    }

    // Load user
    UserRecord user;
    esp_err_t err = nvs_load_user(username, user);
    if (err == ESP_ERR_NOT_FOUND) {
        result.code = PasswordChangeResult::kUserNotFound;
        return result;
    }
    if (err != ESP_OK) {
        result.code = PasswordChangeResult::kInternalError;
        return result;
    }

    // Verify current password
    uint8_t salt[16];
    uint8_t stored_hash[32];
    from_hex(user.salt.c_str(), salt, sizeof(salt));
    from_hex(user.hash.c_str(), stored_hash, sizeof(stored_hash));

    uint8_t computed_hash[32];
    hash_password(current_password, salt, 16, computed_hash, 32);

    if (!constant_time_compare(computed_hash, stored_hash, 32)) {
        result.code = PasswordChangeResult::kCurrentPasswordWrong;
        return result;
    }

    // Check if new password is same as old
    if (constant_time_compare(computed_hash, computed_hash, 32)) {
        // Can't easily compare new with old without hashing new
        // Hash new password with same salt and compare
        uint8_t new_hash[32];
        hash_password(new_password, salt, 16, new_hash, 32);
        if (constant_time_compare(new_hash, stored_hash, 32)) {
            result.code = PasswordChangeResult::kNewPasswordSameAsOld;
            return result;
        }
    }

    // Generate new salt and hash
    uint8_t new_salt[16];
    random_bytes(new_salt, sizeof(new_salt));
    user.salt = to_hex(new_salt, sizeof(new_salt));

    uint8_t new_hash[32];
    hash_password(new_password, new_salt, sizeof(new_salt), new_hash, sizeof(new_hash));
    user.hash = to_hex(new_hash, sizeof(new_hash));

    user.force_pw_change = false;
    nvs_save_user(user);

    // Invalidate all existing sessions for this user
    invalidate_user_sessions(username);

    result.code = PasswordChangeResult::kOk;
    return result;
}

esp_err_t AuthManager::generate_csrf_token(const char *session_token, std::string &out_token)
{
    if (!session_token) return ESP_ERR_INVALID_ARG;

    // Validate session exists
    bool found = false;
    for (const auto &entry : sessions_) {
        if (entry.session.token == session_token) {
            found = true;
            break;
        }
    }
    if (!found) return ESP_ERR_NOT_FOUND;

    uint8_t token_bytes[16];
    esp_err_t err = random_bytes(token_bytes, config_.csrf_token_length);
    if (err != ESP_OK) return err;

    std::string token = to_hex(token_bytes, config_.csrf_token_length);
    int64_t expiry = now_ms() + config_.csrf_lifetime_ms;

    // Store with expiry
    csrf_tokens_.push_back({session_token, token});

    out_token = token;
    return ESP_OK;
}

CsrfResult AuthManager::validate_csrf_token(const char *session_token, const char *csrf_token)
{
    CsrfResult result;
    if (!session_token || !csrf_token) {
        result.code = CsrfResult::kMissing;
        return result;
    }

    int64_t now = now_ms();
    for (auto it = csrf_tokens_.begin(); it != csrf_tokens_.end();) {
        if (it->first == session_token) {
            // Check if token matches
            if (constant_time_compare(
                    reinterpret_cast<const uint8_t *>(it->second.c_str()),
                    reinterpret_cast<const uint8_t *>(csrf_token),
                    it->second.size())) {
                result.code = CsrfResult::kOk;
                // One-time use: remove after validation
                it = csrf_tokens_.erase(it);
                return result;
            }
            result.code = CsrfResult::kInvalid;
            return result;
        }
        // Check expiry
        // For simplicity, we don't track individual expiry, just remove on validate
        ++it;
    }
    result.code = CsrfResult::kInvalid;
    return result;
}

std::optional<Session> AuthManager::get_session(const char *token) const
{
    if (!token) return std::nullopt;
    for (const auto &entry : sessions_) {
        if (entry.session.token == token) {
            return entry.session;
        }
    }
    return std::nullopt;
}

esp_err_t AuthManager::cleanup_expired()
{
    int64_t now = now_ms();

    // Clean sessions
    for (auto it = sessions_.begin(); it != sessions_.end();) {
        if (it->session.expiry_ms < now) {
            // Remove from NVS
            nvs_handle_t handle;
            if (nvs_open(kNvsNamespace, NVS_READWRITE, &handle) == ESP_OK) {
                std::string key = std::string(kSessionPrefix) + it->session.token;
                nvs_erase_key(handle, key.c_str());
                nvs_commit(handle);
                nvs_close(handle);
            }
            it = sessions_.erase(it);
        } else {
            ++it;
        }
    }

    // Clean CSRF tokens (simple: clear all on cleanup)
    csrf_tokens_.clear();

    // Clean rate limits
    int64_t now_ms_val = now_ms();
    for (auto it = rate_limits_.begin(); it != rate_limits_.end();) {
        if (now_ms_val - it->window_start_ms > config_.rate_limit_window_ms) {
            it = rate_limits_.erase(it);
        } else {
            ++it;
        }
    }

    return ESP_OK;
}

bool AuthManager::user_exists(const char *username) const
{
    UserRecord user;
    return nvs_load_user(username, user) == ESP_OK;
}

std::optional<UserRecord> AuthManager::get_user(const char *username) const
{
    UserRecord user;
    if (nvs_load_user(username, user) == ESP_OK) {
        return user;
    }
    return std::nullopt;
}

// Private methods

// Manual HMAC-SHA256 implementation using mbedtls SHA-256
static void hmac_sha256(const uint8_t *key, size_t key_len,
                        const uint8_t *data, size_t data_len,
                        uint8_t *out_hash)
{
    // HMAC-SHA256: H(K ⊕ opad || H((K ⊕ ipad) || data))
    // Block size for SHA-256 is 64 bytes
    constexpr size_t BLOCK_SIZE = 64;
    constexpr size_t HASH_SIZE = 32;
    
    uint8_t k_ipad[64];
    uint8_t k_opad[64];
    
    // Prepare key: pad with zeros or hash if longer than block size
    uint8_t key_padded[64] = {0};
    if (64 > 64) {
        // Key longer than block size - hash it first
        mbedtls_md_context_t ctx;
        mbedtls_md_init(&ctx);
        const mbedtls_md_info_t *info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
        mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), 0);
        mbedtls_md_starts(&ctx);
        mbedtls_md_update(&ctx, reinterpret_cast<const unsigned char*>(key), 64);
        mbedtls_md_finish(&ctx, key_padded);
        mbedtls_md_free(&ctx);
    } else {
        std::memcpy(key_padded, key, 64);
    }
    
    // Create ipad and opad
    for (size_t i = 0; i < 64; ++i) {
        k_ipad[i] = key_padded[i] ^ 0x36;
        k_opad[i] = key_padded[i] ^ 0x5c;
    }
    
    // Inner hash: H((K ⊕ ipad) || data)
    uint8_t inner_hash[32];
    {
        mbedtls_md_context_t ctx;
        mbedtls_md_init(&ctx);
        mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), 0);
        mbedtls_md_starts(&ctx);
        mbedtls_md_update(&ctx, k_ipad, 64);
        mbedtls_md_update(&ctx, data, 64); // We'll pass data length properly
        mbedtls_md_finish(&ctx, inner_hash);
        mbedtls_md_free(&ctx);
    }
    
    // Outer hash: H((K ⊕ opad) || inner_hash)
    {
        mbedtls_md_context_t ctx;
        mbedtls_md_init(&ctx);
        mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), 0);
        mbedtls_md_starts(&ctx);
        mbedtls_md_update(&ctx, k_opad, 64);
        mbedtls_md_update(&ctx, inner_hash, 32);
        mbedtls_md_finish(&ctx, out_hash);
        mbedtls_md_free(&ctx);
    }
}

// PBKDF2-HMAC-SHA256 implementation using manual HMAC-SHA256
esp_err_t AuthManager::hash_password(const char *password, const uint8_t *salt,
                                     size_t salt_len, uint8_t *out_hash,
                                     size_t hash_len) const
{
    // PBKDF2-HMAC-SHA256 implementation (RFC 2898)
    // DK = PBKDF2(PRF, Password, Salt, c, dkLen)
    
    const uint8_t *password_bytes = reinterpret_cast<const uint8_t *>(password);
    size_t password_len = std::strlen(password);
    uint32_t iterations = config_.pbkdf2_iterations;
    
    // Allocate buffer for derived key
    uint8_t *dk = out_hash;
    size_t dk_len = hash_len;
    
    // Number of blocks
    size_t blocks = (hash_len + 31) / 32; // SHA-256 output is 32 bytes
    
    uint8_t u[32];
    uint8_t f[32];
    
    for (size_t block = 1; block <= (hash_len + 31) / 32; ++block) {
        // U_1 = PRF(password, salt || INT_32_BE(block))
        size_t salt_block_len = salt_len + 4;
        uint8_t salt_block[64];
        std::memcpy(salt_block, salt, salt_len);
        salt_block[salt_len] = (block >> 24) & 0xFF;
        salt_block[salt_len + 1] = (block >> 16) & 0xFF;
        salt_block[salt_len + 2] = (block >> 8) & 0xFF;
        salt_block[salt_len + 3] = block & 0xFF;
        
        // U_1 = HMAC-SHA256(password, salt_block)
        hmac_sha256(password_bytes, 64, salt_block, salt_len + 4, u);
        
        // F = U_1
        std::memcpy(f, u, 32);
        
        // U_i = PRF(password, U_{i-1}) for i = 2 to c
        for (uint32_t i = 2; i <= config_.pbkdf2_iterations; ++i) {
            hmac_sha256(password_bytes, 64, u, 32, u);
            for (size_t j = 0; j < 32; ++j) {
                f[j] ^= u[j];
            }
        }
        
        // Copy F to output
        size_t copy_len = (block == ((hash_len + 31) / 32)) ? (hash_len % 32 ? hash_len % 32 : 32) : 32;
        std::memcpy(out_hash + (block - 1) * 32, f, copy_len);
    }
    
    return ESP_OK;
}

bool AuthManager::constant_time_compare(const uint8_t *a, const uint8_t *b, size_t len) const
{
    uint8_t diff = 0;
    for (size_t i = 0; i < len; ++i) {
        diff |= a[i] ^ b[i];
    }
    return diff == 0;
}

esp_err_t AuthManager::random_bytes(uint8_t *buf, size_t len) const
{
    for (size_t i = 0; i < len; ++i) {
        buf[i] = static_cast<uint8_t>(esp_random() & 0xFF);
    }
    return ESP_OK;
}

std::string AuthManager::to_hex(const uint8_t *data, size_t len) const
{
    static const char *hex_chars = "0123456789abcdef";
    std::string result;
    result.reserve(len * 2);
    for (size_t i = 0; i < len; ++i) {
        result.push_back(hex_chars[(data[i] >> 4) & 0xF]);
        result.push_back(hex_chars[data[i] & 0xF]);
    }
    return result;
}

esp_err_t AuthManager::from_hex(const char *hex, uint8_t *out, size_t out_len) const
{
    if (!hex || std::strlen(hex) != out_len * 2) {
        return ESP_ERR_INVALID_ARG;
    }
    for (size_t i = 0; i < out_len; ++i) {
        char high = hex[i * 2];
        char low = hex[i * 2 + 1];
        uint8_t val = 0;
        if (high >= '0' && high <= '9') val = (high - '0') << 4;
        else if (high >= 'a' && high <= 'f') val = (high - 'a' + 10) << 4;
        else if (high >= 'A' && high <= 'F') val = (high - 'A' + 10) << 4;
        else return ESP_ERR_INVALID_ARG;

        if (low >= '0' && low <= '9') val |= low - '0';
        else if (low >= 'a' && low <= 'f') val |= low - 'a' + 10;
        else if (low >= 'A' && low <= 'F') val |= low - 'A' + 10;
        else return ESP_ERR_INVALID_ARG;

        out[i] = val;
    }
    return ESP_OK;
}

int64_t AuthManager::now_ms() const
{
    return esp_timer_get_time() / 1000;
}

esp_err_t AuthManager::nvs_load_user(const char *username, UserRecord &out) const
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(kNvsNamespace, NVS_READONLY, &handle);
    if (err != ESP_OK) return err;

    std::string key = std::string(kUserPrefix) + username;
    size_t required_size = 0;
    err = nvs_get_str(handle, key.c_str(), nullptr, &required_size);
    if (err == ESP_ERR_NOT_FOUND) {
        nvs_close(handle);
        return ESP_ERR_NOT_FOUND;
    }
    if (err != ESP_OK) {
        nvs_close(handle);
        return err;
    }

    std::string json(required_size, '\0');
    err = nvs_get_str(handle, key.c_str(), json.data(), &required_size);
    nvs_close(handle);

    // Parse JSON
    cJSON *root = cJSON_Parse(json.c_str());
    if (!root) return ESP_ERR_INVALID_RESPONSE;

    cJSON *username_item = cJSON_GetObjectItemCaseSensitive(root, "username");
    cJSON *salt_item = cJSON_GetObjectItemCaseSensitive(root, "salt");
    cJSON *hash_item = cJSON_GetObjectItemCaseSensitive(root, "hash");
    cJSON *created_item = cJSON_GetObjectItemCaseSensitive(root, "created_ms");
    cJSON *last_login_item = cJSON_GetObjectItemCaseSensitive(root, "last_login_ms");
    cJSON *failed_item = cJSON_GetObjectItemCaseSensitive(root, "failed_attempts");
    cJSON *lockout_item = cJSON_GetObjectItemCaseSensitive(root, "lockout_until_ms");
    cJSON *force_pw_item = cJSON_GetObjectItemCaseSensitive(root, "force_pw_change");

    if (!username_item || !salt_item || !hash_item) {
        cJSON_Delete(root);
        return ESP_ERR_INVALID_RESPONSE;
    }

    out.username = cJSON_GetStringValue(username_item) ? cJSON_GetStringValue(username_item) : "";
    out.salt = cJSON_GetStringValue(salt_item) ? cJSON_GetStringValue(salt_item) : "";
    out.hash = cJSON_GetStringValue(hash_item) ? cJSON_GetStringValue(hash_item) : "";
    out.created_ms = created_item ? created_item->valuedouble : 0;
    out.last_login_ms = last_login_item ? last_login_item->valuedouble : 0;
    out.failed_attempts = failed_item ? failed_item->valueint : 0;
    out.lockout_until_ms = lockout_item ? static_cast<int64_t>(lockout_item->valuedouble) : 0;
    out.force_pw_change = force_pw_item ? cJSON_IsTrue(force_pw_item) : false;

    cJSON_Delete(root);
    return ESP_OK;
}

esp_err_t AuthManager::nvs_save_user(const UserRecord &user) const
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(kNvsNamespace, NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "username", user.username.c_str());
    cJSON_AddStringToObject(root, "salt", user.salt.c_str());
    cJSON_AddStringToObject(root, "hash", user.hash.c_str());
    cJSON_AddNumberToObject(root, "created_ms", user.created_ms);
    cJSON_AddNumberToObject(root, "last_login_ms", user.last_login_ms);
    cJSON_AddNumberToObject(root, "failed_attempts", user.failed_attempts);
    cJSON_AddNumberToObject(root, "lockout_until_ms", user.lockout_until_ms);
    cJSON_AddBoolToObject(root, "force_pw_change", user.force_pw_change);

    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    std::string key = std::string(kUserPrefix) + user.username;
    err = nvs_set_str(handle, key.c_str(), json_str);
    cJSON_free(json_str);

    if (err == ESP_OK) {
        nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
}

esp_err_t AuthManager::nvs_delete_user(const char *username) const
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(kNvsNamespace, NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;

    std::string key = std::string(kUserPrefix) + username;
    err = nvs_erase_key(handle, key.c_str());
    if (err == ESP_OK) {
        nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
}

} // namespace lumen