// Lumen HTTP API routes (Phase 7 — hardware endpoints + Phase 6 groups).
//
// Registers the SPEC §17.2 endpoint set on a WebServer: boards
// (list/active/get/validate/create/apply/update/delete) and hardware
// (status/inputs/i2c-scan). All responses use the {success,data,error}
// envelope. Board profile serialization + validation live in the
// host-testable board_json core.
#pragma once

#include "esp_err.h"
#include "esp_http_server.h"

namespace lumen
{

class WebServer;
class ConfigManager;
class IStorage;

namespace web
{

class ApiRoutes
{
  public:
    ApiRoutes(WebServer &server, ConfigManager &config, IStorage &storage);
    ~ApiRoutes() = default;

    // Register all Phase 6/7 endpoints. Safe to call once after the
    // WebServer is running. Returns ESP_OK (individual registrations log
    // failures but do not abort the set).
    esp_err_t register_all();

  private:
    WebServer &server_;
    ConfigManager &config_;
    IStorage &storage_;

    // Board handlers (member functions captured via lambdas).
    esp_err_t handle_boards_list(httpd_req_t *req);
    esp_err_t handle_board_active(httpd_req_t *req);
    esp_err_t handle_board_get(httpd_req_t *req);
    esp_err_t handle_board_create(httpd_req_t *req);
    esp_err_t handle_board_validate(httpd_req_t *req);
    esp_err_t handle_board_apply(httpd_req_t *req);
    esp_err_t handle_board_update(httpd_req_t *req);
    esp_err_t handle_board_delete(httpd_req_t *req);

    // Hardware handlers.
    esp_err_t handle_hardware_status(httpd_req_t *req);
    esp_err_t handle_hardware_inputs(httpd_req_t *req);
    esp_err_t handle_hardware_i2c_scan(httpd_req_t *req);

    // Helpers.
    bool is_builtin_board(const char *id) const;
};

} // namespace web
} // namespace lumen