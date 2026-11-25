// Lumen API — authentication (SPEC §16, §17.2).
//
//   POST /auth/login      {username, password}       public
//   POST /auth/logout                                session
//   GET  /auth/session                               session (incl. pw-change)
//   PUT  /auth/password   {currentPassword, newPassword}  session (incl. pw-change)
//   POST /auth/password   (alias of PUT for older clients)
#include "lumen/app/app_context.hpp"
#include "lumen/web/api_util.hpp"

namespace lumen
{

namespace
{

cJSON *session_json(const Session &s)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "username", s.username.c_str());
    cJSON_AddBoolToObject(o, "forcePasswordChange", s.force_pw_change);
    cJSON_AddStringToObject(o, "csrfToken", s.csrf.c_str());
    return o;
}

esp_err_t change_password(AppContext &ctx, ApiRequest &req)
{
    cJSON *body = req.json();
    std::string current, next;
    if (!api::get_string(body, "currentPassword", current, 128) ||
        !api::get_string(body, "newPassword", next, 129)) {
        return send_error(req, 400, "INVALID_REQUEST", "currentPassword and newPassword are required");
    }
    const std::string username = req.session.username;
    const PasswordChangeResult res = ctx.auth->change_password(username, current, next);
    switch (res.code) {
    case PasswordChangeResult::kOk: {
        // All sessions (including this one) are now invalid. Issue a fresh
        // session for the requester so the UI continues without a re-login.
        ctx.ws->revalidate_sessions();
        AuthResult fresh = ctx.auth->login(username, next, req.client_ip);
        if (fresh.code == AuthResult::kOk) {
            WebServer::set_session_cookie(req.raw, fresh.session->token, 7 * 24 * 3600);
            return send_data(req, session_json(*fresh.session));
        }
        WebServer::clear_session_cookie(req.raw);
        return send_ok(req);
    }
    case PasswordChangeResult::kCurrentPasswordWrong:
        return send_error(req, 400, "CURRENT_PASSWORD_WRONG", res.message);
    case PasswordChangeResult::kNewPasswordTooWeak:
    case PasswordChangeResult::kNewPasswordSameAsOld:
        return send_error(req, 400, "WEAK_PASSWORD", res.message);
    default:
        return send_error(req, 500, "INTERNAL_ERROR", "Password could not be changed");
    }
}

} // namespace

void register_auth_routes(AppContext &ctx)
{
    WebServer &web = *ctx.web;

    web.route(HTTP_POST, "/auth/login", RouteAuth::kPublic, [&ctx](ApiRequest &req) {
        cJSON *body = req.json();
        std::string user, pass;
        if (!api::get_string(body, "username", user, 33) || !api::get_string(body, "password", pass, 129)) {
            return send_error(req, 400, "INVALID_REQUEST", "username and password are required");
        }
        AuthResult res = ctx.auth->login(user, pass, req.client_ip);
        switch (res.code) {
        case AuthResult::kOk:
            WebServer::set_session_cookie(req.raw, res.session->token, 7 * 24 * 3600);
            return send_data(req, session_json(*res.session));
        case AuthResult::kRateLimited:
        case AuthResult::kAccountLocked: {
            static thread_local char retry[12];
            std::snprintf(retry, sizeof(retry), "%u", static_cast<unsigned>(res.retry_after_s));
            httpd_resp_set_hdr(req.raw, "Retry-After", retry);
            return send_error(req, 429, "RATE_LIMITED",
                              "Too many login attempts. Try again in " + std::string(retry) + " s");
        }
        case AuthResult::kInvalidCredentials:
            return send_error(req, 401, "INVALID_CREDENTIALS", "Incorrect username or password");
        default:
            return send_error(req, 500, "INTERNAL_ERROR", "Login failed");
        }
    });

    web.route(HTTP_POST, "/auth/logout", RouteAuth::kPasswordChange, [&ctx](ApiRequest &req) {
        ctx.auth->logout(req.session_token);
        ctx.ws->revalidate_sessions();
        WebServer::clear_session_cookie(req.raw);
        return send_ok(req);
    });

    web.route(HTTP_GET, "/auth/session", RouteAuth::kPublic, [](ApiRequest &req) {
        if (req.session_token.empty()) {
            return send_error(req, 401, "UNAUTHORIZED", "Not signed in");
        }
        return send_data(req, session_json(req.session));
    });

    web.route(HTTP_PUT, "/auth/password", RouteAuth::kPasswordChange,
              [&ctx](ApiRequest &req) { return change_password(ctx, req); });
    web.route(HTTP_POST, "/auth/password", RouteAuth::kPasswordChange,
              [&ctx](ApiRequest &req) { return change_password(ctx, req); });
}

} // namespace lumen
