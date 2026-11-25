// Lumen authentication manager implementation. See auth_manager.hpp
#include "lumen/web/auth_manager.hpp"

#include <cstring>

#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "psa/crypto.h"
#include "nvs.h"

namespace lumen
{

namespace
{
const char *TAG = "auth";
constexpr const char *kNamespace = "auth";
constexpr const char *kDefaultUser = "admin";
constexpr const char *kDefaultPassword = "admin";

bool constant_time_equal(const uint8_t *a, const uint8_t *b, std::size_t len)
{
    uint8_t diff = 0;
    for (std::size_t i = 0; i < len; ++i) {
        diff |= static_cast<uint8_t>(a[i] ^ b[i]);
    }
    return diff == 0;
}

bool constant_time_equal(const std::string &a, const std::string &b)
{
    if (a.size() != b.size() || a.empty()) {
        return false;
    }
    return constant_time_equal(reinterpret_cast<const uint8_t *>(a.data()),
                               reinterpret_cast<const uint8_t *>(b.data()), a.size());
}

std::string user_key(const std::string &username)
{
    return "u." + username.substr(0, 12); // NVS keys are <= 15 chars
}
} // namespace

AuthManager::AuthManager(const AuthConfig &config) : config_(config) {}

int64_t AuthManager::now_ms() const
{
    return esp_timer_get_time() / 1000;
}

std::string AuthManager::random_hex(std::size_t bytes)
{
    static const char *hex = "0123456789abcdef";
    uint8_t buf[32];
    if (bytes > sizeof(buf)) {
        bytes = sizeof(buf);
    }
    esp_fill_random(buf, bytes); // hardware RNG (RF subsystem / bootloader entropy)
    std::string out;
    out.reserve(bytes * 2);
    for (std::size_t i = 0; i < bytes; ++i) {
        out.push_back(hex[buf[i] >> 4]);
        out.push_back(hex[buf[i] & 0x0F]);
    }
    return out;
}

esp_err_t AuthManager::hash_password(const std::string &password, const uint8_t *salt,
                                     std::size_t salt_len, uint32_t iterations,
                                     uint8_t out[32]) const
{
    // PBKDF2-HMAC-SHA256 (RFC 8018 §5.2) on the PSA hash API, dkLen = 32
    // (single block). HMAC inner/outer pad states are hashed once and cloned
    // per iteration, so each round costs two SHA-256 compressions.
    if (salt_len > 60 || iterations == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    static bool psa_ready = false;
    if (!psa_ready) {
        if (psa_crypto_init() != PSA_SUCCESS) return ESP_FAIL;
        psa_ready = true;
    }
    constexpr std::size_t kBlock = 64;
    uint8_t key[kBlock] = {};
    std::size_t len = 0;
    if (password.size() > kBlock) {
        if (psa_hash_compute(PSA_ALG_SHA_256, reinterpret_cast<const uint8_t *>(password.data()),
                             password.size(), key, 32, &len) != PSA_SUCCESS) {
            return ESP_FAIL;
        }
    } else {
        std::memcpy(key, password.data(), password.size());
    }
    uint8_t ipad[kBlock], opad[kBlock];
    for (std::size_t i = 0; i < kBlock; ++i) {
        ipad[i] = static_cast<uint8_t>(key[i] ^ 0x36);
        opad[i] = static_cast<uint8_t>(key[i] ^ 0x5c);
    }
    std::memset(key, 0, sizeof(key));

    psa_hash_operation_t inner_base = PSA_HASH_OPERATION_INIT;
    psa_hash_operation_t outer_base = PSA_HASH_OPERATION_INIT;
    psa_status_t st = psa_hash_setup(&inner_base, PSA_ALG_SHA_256);
    if (st == PSA_SUCCESS) st = psa_hash_update(&inner_base, ipad, kBlock);
    if (st == PSA_SUCCESS) st = psa_hash_setup(&outer_base, PSA_ALG_SHA_256);
    if (st == PSA_SUCCESS) st = psa_hash_update(&outer_base, opad, kBlock);
    std::memset(ipad, 0, sizeof(ipad));
    std::memset(opad, 0, sizeof(opad));

    // hmac(msg) = H(opad || H(ipad || msg))
    auto hmac = [&](const uint8_t *msg, std::size_t msg_len, uint8_t mac[32]) -> psa_status_t {
        psa_hash_operation_t op = PSA_HASH_OPERATION_INIT;
        uint8_t inner[32];
        std::size_t n = 0;
        psa_status_t s2 = psa_hash_clone(&inner_base, &op);
        if (s2 == PSA_SUCCESS) s2 = psa_hash_update(&op, msg, msg_len);
        if (s2 == PSA_SUCCESS) s2 = psa_hash_finish(&op, inner, sizeof(inner), &n);
        else psa_hash_abort(&op);
        if (s2 != PSA_SUCCESS) return s2;
        psa_hash_operation_t op2 = PSA_HASH_OPERATION_INIT;
        s2 = psa_hash_clone(&outer_base, &op2);
        if (s2 == PSA_SUCCESS) s2 = psa_hash_update(&op2, inner, sizeof(inner));
        if (s2 == PSA_SUCCESS) s2 = psa_hash_finish(&op2, mac, 32, &n);
        else psa_hash_abort(&op2);
        return s2;
    };

    uint8_t block[64];
    std::memcpy(block, salt, salt_len);
    block[salt_len + 0] = 0;
    block[salt_len + 1] = 0;
    block[salt_len + 2] = 0;
    block[salt_len + 3] = 1; // INT_32_BE(1)
    uint8_t u[32];
    if (st == PSA_SUCCESS) st = hmac(block, salt_len + 4, u);
    if (st == PSA_SUCCESS) std::memcpy(out, u, 32);
    for (uint32_t i = 1; i < iterations && st == PSA_SUCCESS; ++i) {
        st = hmac(u, sizeof(u), u);
        for (int j = 0; j < 32; ++j) {
            out[j] ^= u[j];
        }
    }
    psa_hash_abort(&inner_base);
    psa_hash_abort(&outer_base);
    std::memset(u, 0, sizeof(u));
    return st == PSA_SUCCESS ? ESP_OK : ESP_FAIL;
}

esp_err_t AuthManager::load_user(const std::string &username, UserRecord &out) const
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(kNamespace, NVS_READONLY, &h);
    if (err != ESP_OK) {
        return err == ESP_ERR_NVS_NOT_FOUND ? ESP_ERR_NOT_FOUND : err;
    }
    std::size_t len = sizeof(out);
    err = nvs_get_blob(h, user_key(username).c_str(), &out, &len);
    nvs_close(h);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_ERR_NOT_FOUND;
    }
    if (err == ESP_OK && (len != sizeof(out) || out.version != 1 || out.iterations == 0)) {
        return ESP_ERR_INVALID_SIZE;
    }
    return err;
}

esp_err_t AuthManager::save_user(const std::string &username, const UserRecord &rec) const
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(kNamespace, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_blob(h, user_key(username).c_str(), &rec, sizeof(rec));
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    return err;
}

esp_err_t AuthManager::set_password(UserRecord &rec, const std::string &password) const
{
    esp_fill_random(rec.salt, sizeof(rec.salt));
    rec.iterations = config_.pbkdf2_iterations;
    return hash_password(password, rec.salt, sizeof(rec.salt), rec.iterations, rec.hash);
}

bool AuthManager::verify(const UserRecord &rec, const std::string &password) const
{
    uint8_t computed[32];
    if (hash_password(password, rec.salt, sizeof(rec.salt), rec.iterations, computed) != ESP_OK) {
        return false;
    }
    const bool ok = constant_time_equal(computed, rec.hash, sizeof(computed));
    std::memset(computed, 0, sizeof(computed));
    return ok;
}

esp_err_t AuthManager::init()
{
    std::lock_guard<std::mutex> lock(mutex_);
    UserRecord rec;
    esp_err_t err = load_user(kDefaultUser, rec);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "admin account loaded%s", rec.force_pw_change ? " (password change pending)" : "");
        return ESP_OK;
    }
    ESP_LOGW(TAG, "provisioning default admin account (forced password change): %s",
             esp_err_to_name(err));
    UserRecord fresh;
    fresh.force_pw_change = 1;
    err = set_password(fresh, kDefaultPassword);
    if (err == ESP_OK) {
        err = save_user(kDefaultUser, fresh);
    }
    return err;
}

esp_err_t AuthManager::reset_credentials()
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        UserRecord fresh;
        fresh.force_pw_change = 1;
        esp_err_t err = set_password(fresh, kDefaultPassword);
        if (err == ESP_OK) {
            err = save_user(kDefaultUser, fresh);
        }
        if (err != ESP_OK) {
            return err;
        }
        consecutive_failures_ = 0;
        locked_until_ms_ = 0;
    }
    invalidate_all_sessions();
    return ESP_OK;
}

AuthResult AuthManager::login(const std::string &username, const std::string &password,
                              const std::string &client_ip)
{
    AuthResult result;
    std::lock_guard<std::mutex> lock(mutex_);
    const int64_t now = now_ms();

    if (username.empty() || username.size() > 32 || password.size() > 128) {
        result.code = AuthResult::kInvalidCredentials;
        return result;
    }

    // Per-IP sliding window (bounded table; evict the oldest window).
    RateEntry *entry = nullptr;
    RateEntry *oldest = &rate_[0];
    for (auto &r : rate_) {
        if (!r.ip.empty() && r.ip == client_ip) {
            entry = &r;
            break;
        }
        if (r.window_start_ms < oldest->window_start_ms) {
            oldest = &r;
        }
    }
    if (entry == nullptr) {
        entry = oldest;
        *entry = RateEntry{client_ip, 0, now};
    }
    if (now - entry->window_start_ms >= config_.rate_limit_window_ms) {
        entry->count = 0;
        entry->window_start_ms = now;
    }
    if (entry->count >= config_.max_login_attempts) {
        result.code = AuthResult::kRateLimited;
        result.retry_after_s = static_cast<uint32_t>(
            (config_.rate_limit_window_ms - (now - entry->window_start_ms)) / 1000 + 1);
        return result;
    }
    if (locked_until_ms_ > now) {
        result.code = AuthResult::kAccountLocked;
        result.retry_after_s = static_cast<uint32_t>((locked_until_ms_ - now) / 1000 + 1);
        return result;
    }
    entry->count++;

    UserRecord rec;
    const esp_err_t err = load_user(username, rec);
    bool ok = false;
    if (err == ESP_OK) {
        ok = verify(rec, password);
    } else {
        // Equalise timing for unknown users.
        uint8_t dummy_salt[16] = {};
        uint8_t dummy[32];
        hash_password(password, dummy_salt, sizeof(dummy_salt), config_.pbkdf2_iterations, dummy);
    }
    if (!ok) {
        if (++consecutive_failures_ >= config_.account_lockout_threshold) {
            locked_until_ms_ = now + config_.account_lockout_ms;
            consecutive_failures_ = 0;
            ESP_LOGW(TAG, "account locked for %lld s after repeated failures",
                     static_cast<long long>(config_.account_lockout_ms / 1000));
        }
        ESP_LOGW(TAG, "login failed from %s", client_ip.c_str());
        result.code = AuthResult::kInvalidCredentials;
        return result;
    }
    consecutive_failures_ = 0;
    entry->count = 0;

    Session s;
    s.token = random_hex(32);
    s.csrf = random_hex(16);
    s.username = username;
    s.created_ms = now;
    s.last_seen_ms = now;
    s.force_pw_change = rec.force_pw_change != 0;

    // Store in a free slot, else evict the least recently used session.
    std::optional<Session> *slot = nullptr;
    for (auto &sess : sessions_) {
        if (!sess.has_value()) {
            slot = &sess;
            break;
        }
    }
    if (slot == nullptr) {
        slot = &sessions_[0];
        for (auto &sess : sessions_) {
            if (sess->last_seen_ms < (*slot)->last_seen_ms) {
                slot = &sess;
            }
        }
    }
    *slot = s;
    ESP_LOGI(TAG, "login ok for '%s' from %s%s", username.c_str(), client_ip.c_str(),
             s.force_pw_change ? " (must change password)" : "");
    result.code = AuthResult::kOk;
    result.session = s;
    return result;
}

AuthResult AuthManager::validate_session(const std::string &token)
{
    AuthResult result;
    result.code = AuthResult::kSessionInvalid;
    if (token.size() != 64) {
        return result;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    const int64_t now = now_ms();
    for (auto &sess : sessions_) {
        if (sess.has_value() && constant_time_equal(sess->token, token)) {
            if (now - sess->last_seen_ms > config_.session_idle_ms ||
                now - sess->created_ms > config_.session_absolute_ms) {
                sess.reset();
                result.code = AuthResult::kSessionExpired;
                return result;
            }
            sess->last_seen_ms = now;
            result.code = AuthResult::kOk;
            result.session = *sess;
            return result;
        }
    }
    return result;
}

bool AuthManager::check_csrf(const std::string &token, const std::string &csrf) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto &sess : sessions_) {
        if (sess.has_value() && constant_time_equal(sess->token, token)) {
            return constant_time_equal(sess->csrf, csrf);
        }
    }
    return false;
}

void AuthManager::logout(const std::string &token)
{
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto &sess : sessions_) {
        if (sess.has_value() && constant_time_equal(sess->token, token)) {
            sess.reset();
        }
    }
}

void AuthManager::invalidate_all_sessions()
{
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto &sess : sessions_) {
        sess.reset();
    }
}

void AuthManager::cleanup_expired()
{
    std::lock_guard<std::mutex> lock(mutex_);
    const int64_t now = now_ms();
    for (auto &sess : sessions_) {
        if (sess.has_value() && (now - sess->last_seen_ms > config_.session_idle_ms ||
                                 now - sess->created_ms > config_.session_absolute_ms)) {
            sess.reset();
        }
    }
}

std::size_t AuthManager::session_count() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    std::size_t n = 0;
    for (const auto &sess : sessions_) {
        n += sess.has_value() ? 1 : 0;
    }
    return n;
}

bool AuthManager::default_password_active() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    UserRecord rec;
    return load_user(kDefaultUser, rec) == ESP_OK && rec.force_pw_change != 0;
}

PasswordChangeResult AuthManager::change_password(const std::string &username,
                                                  const std::string &current_password,
                                                  const std::string &new_password)
{
    PasswordChangeResult result;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (new_password.size() < config_.min_password_length || new_password.size() > 128) {
            result.code = PasswordChangeResult::kNewPasswordTooWeak;
            result.message = "Password must be " + std::to_string(config_.min_password_length) +
                             "-128 characters";
            return result;
        }
        if (new_password == kDefaultPassword || new_password == username) {
            result.code = PasswordChangeResult::kNewPasswordTooWeak;
            result.message = "Password must differ from the username and the default";
            return result;
        }
        UserRecord rec;
        const esp_err_t err = load_user(username, rec);
        if (err == ESP_ERR_NOT_FOUND) {
            result.code = PasswordChangeResult::kUserNotFound;
            return result;
        }
        if (err != ESP_OK) {
            result.code = PasswordChangeResult::kInternalError;
            return result;
        }
        if (!verify(rec, current_password)) {
            result.code = PasswordChangeResult::kCurrentPasswordWrong;
            result.message = "Current password is incorrect";
            return result;
        }
        if (verify(rec, new_password)) {
            result.code = PasswordChangeResult::kNewPasswordSameAsOld;
            result.message = "New password must differ from the current one";
            return result;
        }
        UserRecord updated = rec;
        updated.force_pw_change = 0;
        if (set_password(updated, new_password) != ESP_OK ||
            save_user(username, updated) != ESP_OK) {
            result.code = PasswordChangeResult::kInternalError;
            return result;
        }
    }
    invalidate_all_sessions(); // SPEC §16.3: sessions die on password change
    ESP_LOGI(TAG, "password changed for '%s'; all sessions invalidated", username.c_str());
    result.code = PasswordChangeResult::kOk;
    return result;
}

} // namespace lumen
