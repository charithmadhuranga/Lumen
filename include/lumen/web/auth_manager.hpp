// Lumen authentication manager (SPEC §16, AGENTS.md §8).
//
// * Users live in NVS (namespace "auth"), never in JSON config: salted
//   PBKDF2-HMAC-SHA256 (per-user 16-byte salt, iteration count stored with
//   the hash so it can be raised later without invalidating passwords).
// * First boot provisions admin/admin with force_pw_change = true. A
//   session created with that flag can only reach the password endpoints
//   (enforced by the API router).
// * Sessions: 256-bit random token in an HttpOnly SameSite=Strict cookie;
//   idle + absolute timeouts; bounded table (oldest evicted); held in RAM
//   only — a reboot logs everyone out, nothing secret is written to flash.
// * CSRF: synchronizer token per session, returned in the login/session
//   response body and required in X-CSRF-Token on every mutating request.
// * Login rate limiting per client IP + per-account lockout.
//
// Thread-safe (internal mutex): called from the HTTP server task.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>

#include "esp_err.h"

namespace lumen
{

struct Session
{
    std::string token;    // 64 hex chars
    std::string csrf;     // 32 hex chars
    std::string username;
    int64_t created_ms = 0;
    int64_t last_seen_ms = 0;
    bool force_pw_change = false;
};

struct AuthResult
{
    enum Code : uint8_t
    {
        kOk = 0,
        kInvalidCredentials,
        kAccountLocked,
        kSessionExpired,
        kSessionInvalid,
        kRateLimited,
        kInternalError,
    };
    Code code = kInternalError;
    std::optional<Session> session;
    uint32_t retry_after_s = 0; // for kRateLimited / kAccountLocked
};

struct PasswordChangeResult
{
    enum Code : uint8_t
    {
        kOk = 0,
        kCurrentPasswordWrong,
        kNewPasswordTooWeak,
        kNewPasswordSameAsOld,
        kUserNotFound,
        kInternalError,
    };
    Code code = kInternalError;
    std::string message;
};

struct AuthConfig
{
    std::size_t min_password_length = 10;
    int64_t session_idle_ms = 2LL * 60 * 60 * 1000;      // 2 h idle
    int64_t session_absolute_ms = 7LL * 24 * 60 * 60 * 1000; // 7 days max
    int max_login_attempts = 5;                           // per IP per window
    int64_t rate_limit_window_ms = 5LL * 60 * 1000;
    int account_lockout_threshold = 10;                   // consecutive failures
    int64_t account_lockout_ms = 5LL * 60 * 1000;
    uint32_t pbkdf2_iterations = 10000;
};

class AuthManager
{
  public:
    static constexpr std::size_t kMaxSessions = 8;
    static constexpr std::size_t kMaxRateEntries = 16;

    explicit AuthManager(const AuthConfig &config = {});

    // Load / provision the admin account. Requires NVS initialised.
    esp_err_t init();

    AuthResult login(const std::string &username, const std::string &password,
                     const std::string &client_ip);
    // Validates and touches (idle timeout) a session.
    AuthResult validate_session(const std::string &token);
    bool check_csrf(const std::string &token, const std::string &csrf) const;
    void logout(const std::string &token);
    PasswordChangeResult change_password(const std::string &username,
                                         const std::string &current_password,
                                         const std::string &new_password);
    void invalidate_all_sessions();
    void cleanup_expired();
    std::size_t session_count() const;
    bool default_password_active() const;

    // Factory recovery: restore admin/admin with forced change.
    esp_err_t reset_credentials();

    // Exposed for tests / diagnostics (never logs secrets).
    esp_err_t hash_password(const std::string &password, const uint8_t *salt,
                            std::size_t salt_len, uint32_t iterations,
                            uint8_t out[32]) const;

  private:
    struct UserRecord
    {
        uint8_t version = 1;
        uint8_t force_pw_change = 0;
        uint8_t reserved[2] = {};
        uint32_t iterations = 0;
        uint8_t salt[16] = {};
        uint8_t hash[32] = {};
    };
    struct RateEntry
    {
        std::string ip;
        int count = 0;
        int64_t window_start_ms = 0;
    };

    esp_err_t load_user(const std::string &username, UserRecord &out) const;
    esp_err_t save_user(const std::string &username, const UserRecord &rec) const;
    esp_err_t set_password(UserRecord &rec, const std::string &password) const;
    bool verify(const UserRecord &rec, const std::string &password) const;
    int64_t now_ms() const;
    static std::string random_hex(std::size_t bytes);

    AuthConfig config_;
    mutable std::mutex mutex_;
    std::array<std::optional<Session>, kMaxSessions> sessions_{};
    std::array<RateEntry, kMaxRateEntries> rate_{};
    int consecutive_failures_ = 0;
    int64_t locked_until_ms_ = 0;
};

} // namespace lumen
