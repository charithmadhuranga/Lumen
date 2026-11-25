// Lumen — application context: references to every long-lived service,
// handed to the API route modules. Owned by the Controller.
#pragma once

#include <cstdint>
#include <functional>
#include <string>

#include "lumen/automation/automation_engine.h"
#include "lumen/core/config_manager.hpp"
#include "lumen/core/event_bus.hpp"
#include "lumen/core/storage.hpp"
#include "lumen/hardware/board_service.h"
#include "lumen/hardware/hardware_manager.h"
#include "lumen/lighting/group_manager.h"
#include "lumen/lighting/lighting_task.h"
#include "lumen/lighting/room_manager.h"
#include "lumen/lighting/scene_manager.h"
#include "lumen/networking/network_manager.hpp"
#include "lumen/web/auth_manager.hpp"
#include "lumen/web/web_server.hpp"
#include "lumen/web/websocket_manager.hpp"

namespace lumen
{

class MqttService;
class ControlServers;
class OtaService;

struct ProfileApplyOutcome
{
    bool applied = false;
    bool rolled_back = false;
    std::string active_board;
    std::string message;
    ValidationReport report;
};

struct AppContext
{
    EventBus *bus = nullptr;
    IStorage *storage = nullptr;
    ConfigManager *config = nullptr;
    HardwareManager *hardware = nullptr;
    BoardService *boards = nullptr;
    LightingTask *lighting = nullptr;
    RoomManager *rooms = nullptr;
    GroupManager *groups = nullptr;
    SceneManager *scenes = nullptr;
    AuthManager *auth = nullptr;
    WebServer *web = nullptr;
    WebSocketManager *ws = nullptr;
    NetworkManager *network = nullptr;
    AutomationEngine *automation = nullptr;
    MqttService *mqtt = nullptr;
    ControlServers *control = nullptr;
    OtaService *ota = nullptr;

    // Transactional board-profile apply with rollback (SPEC §5.4).
    std::function<ProfileApplyOutcome(const std::string &id, bool force)> apply_profile;
    // Persist lights.json (after light CRUD). Caller holds the model lock.
    std::function<ConfigStatus()> save_lights;
    // Deferred restart (lets the HTTP response go out first).
    std::function<void(uint32_t delay_ms)> schedule_restart;
    // Stop all configuration writes until the next restart (after a restore).
    std::function<void()> freeze_config;
    // Wipe configuration + credentials, then restart.
    std::function<void()> factory_reset;
    // Live diagnostics snapshot (JSON object text).
    std::function<std::string()> diagnostics_json;
    // Current wall clock + sun times for automations.
    std::function<AutoClock()> automation_clock;
    // Location used for sunrise/sunset (persisted in automation.json).
    std::function<bool(double lat, double lon)> set_location;
    std::function<bool(double &lat, double &lon)> get_location;
    // Outbound integrations (bounded queues; never block the caller).
    std::function<bool(const std::string &host, uint16_t port, const std::string &payload, bool tcp)> net_send;
    std::function<bool(const std::string &topic, const std::string &payload, bool retain)> mqtt_publish;
};

// Route registration per API area (src/web/api_*.cpp).
void register_auth_routes(AppContext &ctx);
void register_system_routes(AppContext &ctx);
void register_light_routes(AppContext &ctx);
void register_room_routes(AppContext &ctx);
void register_scene_routes(AppContext &ctx);
void register_hardware_routes(AppContext &ctx);
void register_network_routes(AppContext &ctx);
void register_automation_routes(AppContext &ctx);
void register_integration_routes(AppContext &ctx);
void register_maintenance_routes(AppContext &ctx);

} // namespace lumen
