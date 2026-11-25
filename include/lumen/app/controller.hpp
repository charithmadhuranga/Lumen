// Lumen — controller: owns every long-lived service and the boot sequence.
//
// Task registry (documented in docs/ARCHITECTURE.md):
//   lighting     prio 6, 6 KB, core 1, 20 ms : commands, transitions,
//                effects, batched output flush, input polling
//   housekeeping prio 2, 6 KB, core 0, 250 ms: debounced state persistence,
//                network supervision, session cleanup, status telemetry
//   httpd        prio 5, 10 KB (ESP-IDF)     : REST + WebSocket
// Both Lumen tasks are registered with the task watchdog.
#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "lumen/app/app_context.hpp"
#include "lumen/app/automation_host.hpp"
#include "lumen/networking/control_servers.hpp"
#include "lumen/networking/mqtt_service.hpp"
#include "lumen/networking/net_sender.hpp"
#include <map>
#include "lumen/core/event.hpp"
#include "lumen/core/platform.hpp"
#include "lumen/hardware/platform/esp_driver_factory.h"

namespace lumen
{

class Controller
{
  public:
    Controller() = default;
    // Boots every subsystem and starts the tasks. Never fails as a whole:
    // a broken subsystem is logged and the rest keeps running.
    void start();

  private:
    static void lighting_entry(void *arg);
    static void housekeeping_entry(void *arg);
    void lighting_loop();
    void housekeeping_loop();

    ProfileApplyOutcome apply_profile(const std::string &id, bool force);
    std::string diagnostics_json();
    void factory_reset();
    void load_location();
    bool save_location(double lat, double lon);
    void schedule_restart(uint32_t delay_ms);
    void on_light_changed(const Channel &ch, LightChange change);
    std::string render_light(std::size_t index);

    RtosLock bus_lock_;
    EspClock clock_;
    Event bus_pool_[64];
    std::unique_ptr<EventBus> bus_;
    std::unique_ptr<IStorage> storage_;
    std::unique_ptr<IJson> json_;
    std::unique_ptr<ConfigManager> config_;
    EspDriverFactory factory_;
    std::unique_ptr<HardwareManager> hardware_;
    std::unique_ptr<BoardService> boards_;
    std::unique_ptr<LightingTask> lighting_;
    std::unique_ptr<RoomManager> rooms_;
    std::unique_ptr<GroupManager> groups_;
    std::unique_ptr<SceneManager> scenes_;
    std::unique_ptr<AuthManager> auth_;
    std::unique_ptr<WebServer> web_;
    std::unique_ptr<WebSocketManager> ws_;
    std::unique_ptr<NetworkManager> network_;
    std::unique_ptr<AppAutomationHost> auto_host_;
    std::unique_ptr<AutomationEngine> automation_;
    NetSender net_sender_;
    std::unique_ptr<MqttService> mqtt_;
    std::unique_ptr<ControlServers> control_;
    std::map<std::string, LightState> last_light_states_; // for automation prev-state
    AppContext ctx_;

    void *lighting_task_ = nullptr;     // TaskHandle_t
    void *housekeeping_task_ = nullptr; // TaskHandle_t
    bool fs_ok_ = false;
    uint32_t state_dirty_since_ms_ = 0;
    bool state_pending_ = false;
    bool network_dirty_ = false;
    uint8_t auto_divider_ = 0;
};

} // namespace lumen
