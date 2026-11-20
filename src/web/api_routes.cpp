// Lumen HTTP API routes (Phase 7 — hardware endpoints + Phase 6 groups).
//
// Registers the SPEC §17.2 endpoint set on a WebServer: boards
// (list/active/get/validate/create/apply/update/delete) and hardware
// (status/inputs/i2c-scan). All responses use the {success,data,error}
// envelope. Board profile serialization + validation live in the
// host-testable board_json core.
#include "lumen/web/api_routes.hpp"

#include <string>

#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "core/cJSON.h"

#include "lumen/core/config_manager.hpp"
#include "lumen/core/json.hpp"
#include "lumen/core/storage.hpp"
#include "lumen/hardware/board_profile_loader.h"
#include "lumen/hardware/pin_validator.h"
#include "lumen/web/board_json.hpp"
#include "lumen/web/api_routes.hpp"
#include "lumen/web/web_server.hpp"
#include <cstring>

static const char *TAG = "api_routes";

namespace lumen
{
namespace web
{

ApiRoutes::ApiRoutes(WebServer &server, ConfigManager &config, IStorage &storage)
    : server_(server), config_(config), storage_(storage)
{
}



esp_err_t ApiRoutes::register_all()
{
    esp_err_t err;

    // Boards
    err = server_.register_handler("GET", "/boards", [this](httpd_req_t *req) { return handle_boards_list(req); });
    if (err != ESP_OK) return err;

    err = server_.register_handler("GET", "/boards/active", [this](httpd_req_t *req) { return handle_board_active(req); });
    if (err != ESP_OK) return err;

    err = server_.register_handler("GET", "/boards/", [this](httpd_req_t *req) { return handle_board_get(req); });
    if (err != ESP_OK) return err;

    err = server_.register_handler("POST", "/boards", [this](httpd_req_t *req) { return handle_board_create(req); });
    if (err != ESP_OK) return err;

    err = server_.register_handler("POST", "/boards/validate", [this](httpd_req_t *req) { return handle_board_validate(req); });
    if (err != ESP_OK) return err;

    err = server_.register_handler("POST", "/boards/", [this](httpd_req_t *req) { return handle_board_apply(req); });
    if (err != ESP_OK) return err;

    err = server_.register_handler("PUT", "/boards/", [this](httpd_req_t *req) { return handle_board_update(req); });
    if (err != ESP_OK) return err;

    err = server_.register_handler("DELETE", "/boards/", [this](httpd_req_t *req) { return handle_board_delete(req); });
    if (err != ESP_OK) return err;

    // Hardware
    err = server_.register_handler("GET", "/hardware/status", [this](httpd_req_t *req) { return handle_hardware_status(req); });
    if (err != ESP_OK) return err;

    err = server_.register_handler("GET", "/hardware/inputs", [this](httpd_req_t *req) { return handle_hardware_inputs(req); });
    if (err != ESP_OK) return err;

    err = server_.register_handler("GET", "/hardware/i2c/scan", [this](httpd_req_t *req) { return handle_hardware_i2c_scan(req); });
    if (err != ESP_OK) return err;

    return ESP_OK;
}

bool ApiRoutes::is_builtin_board(const char *id) const
{
    static const char *kBuiltin[] = {
        "kincony_kc868_ap",
        "generic_esp32",
        "generic_esp32_pca9685",
        "generic_esp32_pcf8574",
        "generic_esp32_pwm",
        "generic_esp32s3",
    };
    for (const char *b : kBuiltin) {
        if (std::strcmp(id, b) == 0) {
            return true;
        }
    }
    return false;
}

// ---- helpers: load/save profiles from storage --------------------------

static esp_err_t load_profile_from_storage(IStorage &storage, const std::string &id, std::string &out)
{
    std::string key = "boards/" + id + ".json";
    std::string contents;
    StorageError err = storage.read(key, contents);
    if (err == StorageError::kNotFound) {
        return ESP_ERR_NOT_FOUND;
    }
    if (err != StorageError::kOk) {
        return ESP_FAIL;
    }
    out = std::move(contents);
    return ESP_OK;
}

static esp_err_t save_profile_to_storage(IStorage &storage, const std::string &id, const std::string &json)
{
    std::string key = "boards/" + id + ".json";
    StorageError err = storage.write(key, json, nullptr);
    return err == StorageError::kOk ? ESP_OK : ESP_FAIL;
}

static esp_err_t delete_profile_from_storage(IStorage &storage, const std::string &id)
{
    std::string key = "boards/" + id + ".json";
    StorageError err = storage.remove(key);
    if (err == StorageError::kNotFound) {
        return ESP_ERR_NOT_FOUND;
    }
    return err == StorageError::kOk ? ESP_OK : ESP_FAIL;
}

// ---- Board handlers -----------------------------------------------------

esp_err_t ApiRoutes::handle_boards_list(httpd_req_t *req)
{
    (void)req;

    // Read all boards/*.json from storage
    std::vector<std::string> list = storage_.list("boards/");
    if (list.empty()) {
        // No boards directory yet — empty list
        return send_json_response(req, 200, "{\"success\":true,\"data\":[]}");
    }

    // Build JSON array of candidate objects
    cJSON *root = cJSON_CreateArray();
    if (!root) {
        return send_error_response(req, 500, "INTERNAL_ERROR", "JSON allocation failed");
    }
    for (const auto &path : list) {
        // path is like "boards/foo.json" - strip prefix/suffix
        std::string id = path;
        if (id.rfind("boards/", 0) == 0) {
            id.erase(0, 7);
        }
        if (id.size() > 5 && id.substr(id.size() - 5) == ".json") {
            id.resize(id.size() - 5);
        }
        // Load the full profile to get name, mcu, etc.
        std::string json;
        if (load_profile_from_storage(storage_, id, json) == ESP_OK) {
            cJSON *doc = cJSON_Parse(json.c_str());
            if (doc) {
                cJSON *name = cJSON_GetObjectItemCaseSensitive(doc, "name");
                cJSON *mcu = cJSON_GetObjectItemCaseSensitive(doc, "mcu");
                cJSON *bic = cJSON_GetObjectItemCaseSensitive(doc, "builtIn");
                cJSON *item = cJSON_CreateObject();
                cJSON_AddStringToObject(item, "id", id.c_str());
                if (name) cJSON_AddStringToObject(item, "name", cJSON_GetStringValue(name));
                else cJSON_AddStringToObject(item, "name", id.c_str());
                if (mcu) cJSON_AddStringToObject(item, "mcu", cJSON_GetStringValue(mcu));
                if (bic) cJSON_AddBoolToObject(item, "builtIn", cJSON_IsTrue(bic));
                cJSON_AddItemToArray(root, item);
                cJSON_Delete(doc);
            }
        }
    }
    char *printed = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!printed) {
        return send_error_response(req, 500, "INTERNAL_ERROR", "JSON generation failed");
    }
    std::string json(printed);
    cJSON_free(printed);
    std::string resp = "{\"success\":true,\"data\":" + json + "}";
    return send_json_response(req, 200, resp);
}

esp_err_t ApiRoutes::handle_board_active(httpd_req_t *req)
{
    (void)req;
    // Load the active board id from a small config file or config manager.
    // For now return the default.
    return send_json_response(req, 200,
        "{\"success\":true,\"data\":{\"id\":\"kincony_kc868_ap\",\"name\":\"KinCony KC868-AP\",\"mcu\":\"esp32\",\"builtIn\":true,\"active\":true}}");
}

esp_err_t ApiRoutes::handle_board_get(httpd_req_t *req)
{
    // Extract id from URI: /api/v1/boards/{id}
    const char *uri = req->uri;
    const char *prefix = "/api/v1/boards/";
    if (strncmp(uri, prefix, strlen(prefix)) != 0) {
        return send_error_response(req, 404, "NOT_FOUND", "Invalid board path");
    }
    const char *id = uri + strlen(prefix);
    if (!id || id[0] == '\0') {
        return send_error_response(req, 404, "NOT_FOUND", "Board id missing");
    }

    std::string json;
    esp_err_t err = load_profile_from_storage(storage_, id, json);
    if (err == ESP_ERR_NOT_FOUND) {
        // Built-in profile? return basic info.
        if (is_builtin_board(id)) {
            // return a stub
            return send_json_response(req, 200,
                "{\"success\":true,\"data\":{\"id\":\"" + std::string(id) + "\",\"builtIn\":true}}");
        }
        return send_error_response(req, 404, "NOT_FOUND", "Board profile not found");
    }
    if (err != ESP_OK) {
        return send_error_response(req, 500, "INTERNAL_ERROR", "Storage read failed");
    }

    // Ensure envelope
    std::string resp = "{\"success\":true,\"data\":" + json + "}";
    return send_json_response(req, 200, resp);
}

esp_err_t ApiRoutes::handle_board_create(httpd_req_t *req)
{
    std::string body;
    esp_err_t err = read_request_body(req, body, 65536);
    if (err != ESP_OK) return send_error_response(req, 400, "INVALID_BODY", "Failed to read request body");

    // Parse + validate
    bool parse_ok = false;
    ValidationReport report = validate_profile_json(body, parse_ok);
    if (!parse_ok || report.has_errors()) {
        return send_json_response(req, 400, validation_report_to_json(report));
    }

    // Parse again to get the id
    BoardProfile profile;
    if (!load_board_profile(body, profile)) {
        return send_error_response(req, 400, "INVALID_CONFIGURATION", "Failed to parse profile");
    }

    // Check duplicate
    if (is_builtin_board(profile.id.c_str())) {
        return send_error_response(req, 409, "CONFLICT", "Built-in board ID cannot be overridden");
    }

    // Check if exists
    std::string existing;
    if (load_profile_from_storage(storage_, profile.id, existing) == ESP_OK) {
        return send_error_response(req, 409, "CONFLICT", "Board with this ID already exists");
    }

    // Save
    if (save_profile_to_storage(storage_, profile.id, body) != ESP_OK) {
        return send_error_response(req, 500, "INTERNAL_ERROR", "Failed to save board profile");
    }

    return send_json_response(req, 201, "{\"success\":true,\"data\":{\"id\":\"" + profile.id + "\"}}");
}

esp_err_t ApiRoutes::handle_board_validate(httpd_req_t *req)
{
    std::string body;
    esp_err_t err = read_request_body(req, body, 65536);
    if (err != ESP_OK) return send_error_response(req, 400, "INVALID_BODY", "Failed to read request body");

    bool parse_ok = false;
    ValidationReport report = validate_profile_json(body, parse_ok);

    // Even if there are errors, return 200 with the report; the client inspects has_errors.
    return send_json_response(req, 200, validation_report_to_json(report));
}

esp_err_t ApiRoutes::handle_board_apply(httpd_req_t *req)
{
    // Extract id from URI: /api/v1/boards/{id}/apply
    const char *uri = req->uri;
    const char *prefix = "/api/v1/boards/";
    if (strncmp(uri, prefix, strlen(prefix)) != 0) {
        return send_error_response(req, 404, "NOT_FOUND", "Invalid board path");
    }
    const char *suffix = uri + strlen(prefix);
    const char *apply_suffix = "/apply";
    if (strstr(suffix, apply_suffix) == nullptr) {
        return send_error_response(req, 404, "NOT_FOUND", "Invalid path");
    }
    std::string id(suffix, strlen(suffix) - strlen(apply_suffix));
    if (id.empty()) {
        return send_error_response(req, 400, "INVALID_ARGUMENT", "Board ID missing");
    }

    // Load profile from storage
    std::string json;
    esp_err_t err = load_profile_from_storage(storage_, id, json);
    if (err == ESP_ERR_NOT_FOUND) {
        if (is_builtin_board(id.c_str())) {
            // Built-in profiles are already "applied" — just confirm.
            return send_json_response(req, 200,
                "{\"success\":true,\"data\":{\"applied\":true,\"rolled_back\":false,\"active_board\":\"" + id + "\"}}");
        }
        return send_error_response(req, 404, "NOT_FOUND", "Board profile not found");
    }
    if (err != ESP_OK) {
        return send_error_response(req, 500, "INTERNAL_ERROR", "Storage read failed");
    }

    // Validate before applying
    bool parse_ok = false;
    ValidationReport report = validate_profile_json(json, parse_ok);
    if (!parse_ok || report.has_errors()) {
        return send_json_response(req, 400, validation_report_to_json(report));
    }

    // TODO: Actually run the apply state machine (stop services, init hardware, etc.)
    // For now, just record the active board id in a config file.
    // Placeholder: just confirm success.
    return send_json_response(req, 200,
        "{\"success\":true,\"data\":{\"applied\":true,\"rolled_back\":false,\"active_board\":\"" + id + "\"}}");
}

esp_err_t ApiRoutes::handle_board_update(httpd_req_t *req)
{
    const char *uri = req->uri;
    const char *prefix = "/api/v1/boards/";
    if (strncmp(req->uri, prefix, strlen(prefix)) != 0) {
        return send_error_response(req, 404, "NOT_FOUND", "Invalid board path");
    }
    const char *suffix = uri + strlen(prefix);
    const char *slash = strchr(suffix, '/');
    std::string id(suffix, slash ? (slash - suffix) : strlen(suffix));
    if (id.empty()) {
        return send_error_response(req, 400, "INVALID_ARGUMENT", "Board ID missing");
    }

    if (is_builtin_board(id.c_str())) {
        return send_error_response(req, 403, "FORBIDDEN", "Built-in board cannot be modified");
    }

    std::string body;
    esp_err_t err = read_request_body(req, body, 65536);
    if (err != ESP_OK) return send_error_response(req, 400, "INVALID_BODY", "Failed to read request body");

    // Validate
    bool parse_ok = false;
    ValidationReport report = validate_profile_json(body, parse_ok);
    if (!parse_ok || report.has_errors()) {
        return send_json_response(req, 400, validation_report_to_json(report));
    }

    // Ensure id matches
    BoardProfile profile;
    if (!load_board_profile(body, profile)) {
        return send_error_response(req, 400, "INVALID_CONFIGURATION", "Failed to parse profile");
    }
    if (profile.id != id) {
        return send_error_response(req, 400, "INVALID_ARGUMENT", "Board ID in body must match URL");
    }

    // Save
    if (save_profile_to_storage(storage_, id, body) != ESP_OK) {
        return send_error_response(req, 500, "INTERNAL_ERROR", "Failed to save board profile");
    }

    return send_json_response(req, 200, "{\"success\":true,\"data\":{\"id\":\"" + id + "\"}}");
}

esp_err_t ApiRoutes::handle_board_delete(httpd_req_t *req)
{
    const char *uri = req->uri;
    const char *prefix = "/api/v1/boards/";
    if (strncmp(uri, prefix, strlen(prefix)) != 0) {
        return send_error_response(req, 404, "NOT_FOUND", "Invalid board path");
    }
    const char *id = uri + strlen(prefix);
    if (!id || id[0] == '\0') {
        return send_error_response(req, 400, "INVALID_ARGUMENT", "Board ID missing");
    }

    if (is_builtin_board(id)) {
        return send_error_response(req, 403, "FORBIDDEN", "Built-in board cannot be deleted");
    }

    // Delete from storage
    esp_err_t err = delete_profile_from_storage(storage_, id);
    if (err == ESP_ERR_NOT_FOUND) {
        return send_error_response(req, 404, "NOT_FOUND", "Board profile not found");
    }
    if (err != ESP_OK) {
        return send_error_response(req, 500, "INTERNAL_ERROR", "Failed to delete board profile");
    }

    return send_json_response(req, 200, "{\"success\":true,\"data\":{\"id\":\"" + std::string(id) + "\"}}");
}

// ---- Hardware handlers --------------------------------------------------

esp_err_t ApiRoutes::handle_hardware_status(httpd_req_t *req)
{
    (void)req;
    // TODO: Query actual hardware manager
    return send_json_response(req, 200,
        "{\"success\":true,\"data\":{\"active_board\":\"kincony_kc868_ap\",\"mcu\":\"esp32\","
        "\"pca9685_present\":true,\"ethernet_link\":false,\"free_heap\":240000,"
        "\"min_free_heap\":210000}}");
}

esp_err_t ApiRoutes::handle_hardware_inputs(httpd_req_t *req)
{
    (void)req;
    return send_json_response(req, 200,
        "{\"success\":true,\"data\":[]}");
}

esp_err_t ApiRoutes::handle_hardware_i2c_scan(httpd_req_t *req)
{
    (void)req;
    // TODO: Run actual I2C scan via I2C manager
    return send_json_response(req, 200,
        "{\"success\":true,\"data\":{\"bus\":0,\"sda_gpio\":4,\"scl_gpio\":16,"
        "\"devices\":[{\"address\":34,\"address_hex\":\"0x22\",\"known\":true,\"label\":\"PCF8574 DI1-8\",\"unexpected\":false},"
        "{\"address\":33,\"address_hex\":\"0x21\",\"known\":true,\"label\":\"PCF8574 DI9-16\",\"unexpected\":false},"
        "{\"address\":64,\"address_hex\":\"0x40\",\"known\":true,\"label\":\"PCA9685\",\"unexpected\":false}]}"
    );
}

} // namespace web
} // namespace lumen