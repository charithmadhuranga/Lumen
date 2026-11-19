// Lumen authentication manager — PBKDF2-HMAC-SHA256 with per-user salt,
// secure session tokens, HTTP-only cookies, CSRF protection.
// SPEC §16, §16.1–16.3, §17.1, DESIGN §13.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <optional>
#include <vector>
#include <utility>

#include "esp_err.h"

namespace lumen
{

// Session information
struct Session
{
    std::string token;       // Opaque session token
    std::string username;    // Associated username
    int64_t created_ms = 0;  // Creation timestamp
    int64_t expiry_ms = 0;   // Expiry timestamp
    bool force_pw_change = false; // Force password change on next login
};

// Authentication result
struct AuthResult
{
    enum Code : uint8_t
    {
        kOk = 0,
        kInvalidCredentials = 1,
        kUserNotFound = 2,
        kAccountLocked = 3,
        kSessionExpired = 4,
        kSessionInvalid = 5,
        kRateLimited = 6,
        kInternalError = 7,
    };
    Code code = kInternalError;
    std::optional<Session> session;
};

// Password change result
struct PasswordChangeResult
{
    enum Code : uint8_t
    {
        kOk = 0,
        kCurrentPasswordWrong = 1,
        kNewPasswordTooWeak = 2,
        kNewPasswordSameAsOld = 3,
        kUserNotFound = 4,
        kInternalError = 5,
    };
    Code code = kInternalError;
};

// CSRF token validation result
struct CsrfResult
{
    enum Code : uint8_t
    {
        kOk = 0,
        kMissing = 1,
        kInvalid = 2,
        kExpired = 3,
    };
    Code code = kInvalid;
};

// Configuration for auth manager
struct AuthConfig
{
    // Minimum password length (default: 10)
    std::size_t min_password_length = 10;

    // Session lifetime in milliseconds (default: 24 hours)
    int64_t session_lifetime_ms = 24 * 60 * 60 * 1000;

    // CSRF token lifetime in milliseconds (default: 1 hour)
    int64_t csrf_lifetime_ms = 60 * 60 * 1000;

    // Login rate limit: max attempts per window
    int max_login_attempts = 5;

    // Rate limit window in milliseconds (default: 15 minutes)
    int64_t rate_limit_window_ms = 15 * 60 * 1000;

    // PBKDF2 iterations (default: 100000)
    int pbkdf2_iterations = 100000;

    // Salt length in bytes (default: 16)
    std::size_t salt_length = 16;

    // Session token length in bytes (default: 32)
    std::size_t session_token_length = 32;

    // CSRF token length in bytes (default: 16)
    std::size_t csrf_token_length = 16;
};

// User record stored in NVS
struct UserRecord
{
    std::string username;
    std::string salt;        // Hex-encoded salt
    std::string hash;        // Hex-encoded PBKDF2 hash
    int64_t created_ms = 0;
    int64_t last_login_ms = 0;
    int failed_attempts = 0;
    int64_t lockout_until_ms = 0;
    bool force_pw_change = false;
};

class AuthManager
{
  public:
    explicit AuthManager(const AuthConfig &config = {});
    ~AuthManager();

    // Initialize NVS namespace and load/create default admin user
    esp_err_t init();

    // Authenticate user with username/password
    // Returns session with token on success
    AuthResult login(const char *username, const char *password, const char *ip);

    // Validate session token (from cookie)
    AuthResult validate_session(const char *token);

    // Create new session for user
    esp_err_t create_session(const char *username, Session &out_session, bool force_pw_change);

    // Invalidate session (logout)
    esp_err_t invalidate_session(const char *token);

    // Invalidate all sessions for user
    esp_err_t invalidate_user_sessions(const char *username);

    // Change password (requires current password)
    PasswordChangeResult change_password(const char *username,
                                         const char *current_password,
                                         const char *new_password);

    // Generate CSRF token for session
    esp_err_t generate_csrf_token(const char *session_token, std::string &out_token);

    // Validate CSRF token
    CsrfResult validate_csrf_token(const char *session_token, const char *csrf_token);

    // Get session info (for admin/debug)
    std::optional<Session> get_session(const char *token) const;

    // Clean up expired sessions (call periodically)
    esp_err_t cleanup_expired();

    // Check if user exists
    bool user_exists(const char *username) const;

    // Get user record (for admin)
    std::optional<UserRecord> get_user(const char *username) const;

  private:
    AuthConfig config_;

    // PBKDF2-HMAC-SHA256
    esp_err_t hash_password(const char *password, const uint8_t *salt, size_t salt_len,
                            uint8_t *out_hash, size_t hash_len) const;

    // Constant-time comparison
    bool constant_time_compare(const uint8_t *a, const uint8_t *b, size_t len) const;

    // Generate secure random bytes
    esp_err_t random_bytes(uint8_t *buf, size_t len) const;

    // Encode binary to hex
    std::string to_hex(const uint8_t *data, size_t len) const;

    // Decode hex to binary
    esp_err_t from_hex(const char *hex, uint8_t *out, size_t out_len) const;

    // Current time in ms
    int64_t now_ms() const;

    // NVS operations
    esp_err_t nvs_load_user(const char *username, UserRecord &out) const;
    esp_err_t nvs_save_user(const UserRecord &user) const;
    esp_err_t nvs_delete_user(const char *username) const;

    // Session store (in-memory with NVS backup)
    struct SessionEntry
    {
        Session session;
        bool dirty = false; // Needs NVS sync
    };
    mutable std::vector<SessionEntry> sessions_;
    mutable std::vector<std::pair<std::string, std::string>> csrf_tokens_; // session_token -> csrf_token

    // Rate limiting
    struct RateLimitEntry
    {
        std::string key;        // IP or username
        int count = 0;
        int64_t window_start_ms = 0;
    };
    std::vector<RateLimitEntry> rate_limits_;
};

} // namespace lumen