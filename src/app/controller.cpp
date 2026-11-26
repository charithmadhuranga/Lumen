// Lumen — controller implementation. See controller.hpp
#include "lumen/app/controller.hpp"
#include "lumen/networking/espnow_manager.hpp"

#include <cstring>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_idf_version.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

#include "lumen/core/log_capture.hpp"
#include "lumen/core/model_lock.hpp"
#include "lumen/automation/automation_store.h"
#include "lumen/lighting/light_store.h"
#include "lumen/services/backup.h"
#include "lumen/version.hpp"
#include "lumen/web/api_util.hpp"

namespace lumen
{

namespace
{
Controller *g_controller = nullptr; // for the esp_restart() shutdown handler
uint32_t g_boot_count = 0;
const char *reset_reason_name(esp_reset_reason_t r)
{
    switch (r) {
    case ESP_RST_POWERON: return "power_on";
    case ESP_RST_EXT: return "external_pin";
    case ESP_RST_SW: return "software";
    case ESP_RST_PANIC: return "panic";
    case ESP_RST_INT_WDT: return "interrupt_watchdog";
    case ESP_RST_TASK_WDT: return "task_watchdog";
    case ESP_RST_WDT: return "other_watchdog";
    case ESP_RST_DEEPSLEEP: return "deep_sleep";
    case ESP_RST_BROWNOUT: return "brownout";
    case ESP_RST_SDIO: return "sdio";
    default: return "unknown";
    }
}

// Boot counter in NVS ("sys"/"boots"): lets a soak test spot every reboot.
uint32_t count_boot()
{
    nvs_handle_t h;
    uint32_t boots = 0;
    if (nvs_open("sys", NVS_READWRITE, &h) != ESP_OK) return 0;
    nvs_get_u32(h, "boots", &boots); // missing on first boot
    ++boots;
    if (nvs_set_u32(h, "boots", boots) == ESP_OK) nvs_commit(h);
    nvs_close(h);
    return boots;
}

}


cJSON *network_status_json(const NetworkStatus &n); // api_network.cpp

namespace
{
const char *TAG = "lumen";
constexpr uint32_t kLightingPeriodMs = 20;   // 50 Hz output updates
constexpr uint32_t kStateSaveDebounceMs = 5000;

void remove_tree(const std::string &path)
{
    DIR *d = opendir(path.c_str());
    if (d == nullptr) {
        unlink(path.c_str());
        return;
    }
    while (struct dirent *e = readdir(d)) {
        const std::string name = e->d_name;
        if (name == "." || name == "..") continue;
        const std::string child = path + "/" + name;
        struct stat st{};
        if (stat(child.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) {
            remove_tree(child);
        } else {
            unlink(child.c_str());
        }
    }
    closedir(d);
    rmdir(path.c_str());
}

void register_docs(ConfigManager &config)
{
    for (const char *doc : {"rooms.json", "groups.json", "scenes.json", "automation.json"}) {
        DocConfig cfg{doc, 1, migrate_additive, nullptr, nullptr};
        config.register_document(cfg);
    }
    light_store::register_documents(config);
}
} // namespace

void Controller::start()
{
    ESP_LOGI(TAG, "**** Lumen firmware v%s (ESP-IDF %s) ****", kVersion, esp_get_idf_version());

    // 1. Persistent storage.
    esp_err_t err = platform_init_nvs();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS init failed: %s (credentials unavailable)", esp_err_to_name(err));
    }
    log_capture_install(); // before anything else logs
    g_boot_count = count_boot();
    ESP_LOGI(TAG, "boot #%lu, reset reason: %s", static_cast<unsigned long>(g_boot_count), reset_reason_name(esp_reset_reason()));
    bool formatted = false;
    fs_ok_ = platform_mount_fs(formatted) == ESP_OK;
    if (formatted) {
        ESP_LOGW(TAG, "storage was formatted (first boot or corruption) - starting from defaults");
    }

    // An interrupted backup restore (power loss between the two renames)
    // is repaired before anything reads the configuration.
    if (fs_ok_ && backup::recover_interrupted(kConfigRoot)) {
        ESP_LOGW(TAG, "repaired an interrupted configuration restore");
    }
    ota_ = std::make_unique<OtaService>();
    ota_->boot();

    bus_ = std::make_unique<EventBus>(bus_lock_, clock_, bus_pool_, 64);
    storage_.reset(create_posix_fs(kConfigRoot));
    json_.reset(create_cjson_adapter());
    config_ = std::make_unique<ConfigManager>(*storage_, *json_, *bus_);
    register_docs(*config_);

    // 2. Hardware from the active board profile (falls back to the default
    //    built-in profile if the stored one is missing/invalid).
    boards_ = std::make_unique<BoardService>(*storage_, *config_);
    boards_->init();
    BoardProfile profile;
    if (!boards_->load(boards_->active_id(), profile)) {
        ESP_LOGE(TAG, "active profile '%s' unusable; falling back to %s", boards_->active_id().c_str(),
                 BoardService::kDefaultBoard);
        boards_->load(BoardService::kDefaultBoard, profile);
    }
    hardware_ = std::make_unique<HardwareManager>(factory_);
    {
        ModelLock lock(model_mutex());
        hardware_->start(profile);
    }

    // 3. Lighting model: lights, last state, rooms/groups/scenes.
    lighting_ = std::make_unique<LightingTask>();
    lighting_->set_clock_us([]() { return static_cast<uint64_t>(esp_timer_get_time()); });
    {
        ModelLock lock(model_mutex());
        light_store::load_lights(*config_, *lighting_);
        light_store::load_states(*config_, *lighting_);
        lighting_->attach_hardware(hardware_.get());
        lighting_->tick(uptime_ms()); // apply startup states immediately
        rooms_ = std::make_unique<RoomManager>(lighting_->channel_manager(), *lighting_, *config_);
        groups_ = std::make_unique<GroupManager>(lighting_->channel_manager(), *lighting_, *config_);
        scenes_ = std::make_unique<SceneManager>(lighting_->channel_manager(), *lighting_, *config_);
        ESP_LOGI(TAG, "lighting: %u lights, %u rooms, %u groups, %u scenes",
                 static_cast<unsigned>(lighting_->channel_manager().channel_count()),
                 static_cast<unsigned>(rooms_->room_count()),
                 static_cast<unsigned>(groups_->group_count()),
                 static_cast<unsigned>(scenes_->scene_count()));
    }

    // 4. Security + network + web.
    auth_ = std::make_unique<AuthManager>();
    if (auth_->init() != ESP_OK) {
        ESP_LOGE(TAG, "auth init failed - management API will reject logins");
    }
    network_ = std::make_unique<NetworkManager>();
    network_->set_listener([](void *user) { static_cast<Controller *>(user)->network_dirty_ = true; }, this);
    if (network_->start(profile) != ESP_OK) {
        ESP_LOGE(TAG, "network start failed - lighting continues offline");
    }

    ws_ = std::make_unique<WebSocketManager>();
    web_ = std::make_unique<WebServer>(*auth_);
    web_->set_ws_manager(ws_.get());

    ctx_.bus = bus_.get();
    ctx_.storage = storage_.get();
    ctx_.config = config_.get();
    ctx_.hardware = hardware_.get();
    ctx_.boards = boards_.get();
    ctx_.lighting = lighting_.get();
    ctx_.rooms = rooms_.get();
    ctx_.groups = groups_.get();
    ctx_.scenes = scenes_.get();
    ctx_.auth = auth_.get();
    ctx_.web = web_.get();
    ctx_.ws = ws_.get();
    ctx_.network = network_.get();
    ctx_.apply_profile = [this](const std::string &id, bool force) { return apply_profile(id, force); };
    ctx_.save_lights = [this]() { return light_store::save_lights(*config_, *lighting_); };
    ctx_.schedule_restart = [this](uint32_t ms) { schedule_restart(ms); };
    ctx_.factory_reset = [this]() { factory_reset(); };
    ctx_.freeze_config = [this]() { config_frozen_ = true; };
    ctx_.ota = ota_.get();
    ctx_.diagnostics_json = [this]() { return diagnostics_json(); };
    ctx_.net_send = [this](const std::string &host, uint16_t port, const std::string &payload, bool tcp) {
        return net_sender_.enqueue(host, port, payload, tcp);
    };

    // Integrations (Phase 10): MQTT/HA and TCP/UDP control.
    mqtt_ = std::make_unique<MqttService>(ctx_);
    control_ = std::make_unique<ControlServers>(ctx_);
    ctx_.mqtt = mqtt_.get();
    ctx_.control = control_.get();
    espnow_ = std::make_unique<EspNowManager>(ctx_);
    ctx_.espnow = espnow_.get();
    ctx_.mqtt_publish = [this](const std::string &topic, const std::string &payload, bool retain) {
        return mqtt_->publish(topic, payload, retain);
    };

    // Automations (Phase 9): engine + persisted definitions + location.
    auto_host_ = std::make_unique<AppAutomationHost>(ctx_);
    automation_ = std::make_unique<AutomationEngine>(*auto_host_);
    automation_->set_clock_us([]() { return static_cast<uint64_t>(esp_timer_get_time()); });
    ctx_.automation = automation_.get();
    ctx_.automation_clock = [this]() { return auto_host_->clock(); };
    ctx_.set_location = [this](double lat, double lon) { return save_location(lat, lon); };
    ctx_.get_location = [this](double &lat, double &lon) { return auto_host_->location(lat, lon); };
    load_location();
    {
        ModelLock lock(model_mutex());
        std::string errors;
        const int n = automation_store::load_all(*storage_, *automation_, errors);
        ESP_LOGI(TAG, "automations: %d loaded%s%s", n, errors.empty() ? "" : " - skipped: ", errors.c_str());
        for (std::size_t i = 0; i < lighting_->channel_manager().channel_count(); ++i) {
            const Channel &ch = lighting_->channel_manager().at(i);
            last_light_states_[ch.id] = ch.requested;
        }
    }
    automation_->set_listener(
        [](const HistoryEntry &h, void *user) {
            auto *self = static_cast<Controller *>(user);
            cJSON *o = cJSON_CreateObject();
            cJSON_AddStringToObject(o, "automationId", h.automation_id.c_str());
            cJSON_AddStringToObject(o, "trigger", h.trigger.c_str());
            cJSON_AddStringToObject(o, "result", h.result.c_str());
            cJSON_AddStringToObject(o, "message", h.message.c_str());
            cJSON_AddNumberToObject(o, "durationMs", h.duration_ms);
            const std::string text = api::to_string(o);
            self->ws_->broadcast("automation_executed", text);
            self->mqtt_->automation_status(h.automation_id, text);
            cJSON_Delete(o);
            if (h.result != "ok") {
                ESP_LOGW(TAG, "automation %s: %s %s", h.automation_id.c_str(), h.result.c_str(), h.message.c_str());
            }
        },
        this);

    register_auth_routes(ctx_);
    register_system_routes(ctx_);
    register_light_routes(ctx_);
    register_room_routes(ctx_);
    register_scene_routes(ctx_);
    register_hardware_routes(ctx_);
    register_network_routes(ctx_);
    register_automation_routes(ctx_);
    register_integration_routes(ctx_);
    register_maintenance_routes(ctx_);
    register_floorplan_routes(ctx_);
    register_controller_routes(ctx_);

    if (web_->start() == ESP_OK) {
        ws_->attach(web_->handle());
        ws_->set_light_renderer([this](std::size_t index) { return render_light(index); });
        ws_->set_session_check([this](const std::string &token) {
            AuthResult r = auth_->validate_session(token);
            return r.code == AuthResult::kOk && !r.session->force_pw_change;
        });
    } else {
        ESP_LOGE(TAG, "web server failed to start");
    }
    lighting_->set_listener(
        [](const Channel &ch, LightChange change, void *user) {
            static_cast<Controller *>(user)->on_light_changed(ch, change);
        },
        this);

    mqtt_->load();
    control_->load();
    if (mqtt_->start() != ESP_OK) ESP_LOGW(TAG, "MQTT start failed (see Integrations)");
    // Every esp_restart() path (API, OTA, profile apply): persist the light
    // state still inside the save debounce window and leave a clean retained
    // "offline" instead of waiting for the broker's LWT timeout.
    g_controller = this;
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_register_shutdown_handler([] {
        if (g_controller != nullptr) g_controller->on_shutdown();
    }));
    control_->start();
    espnow_->load();
    if (espnow_->start() != ESP_OK) ESP_LOGW(TAG, "ESP-NOW task could not start");

    // 5. Tasks.
    TaskHandle_t lt = nullptr;
    TaskHandle_t ht = nullptr;
    xTaskCreatePinnedToCore(&Controller::lighting_entry, "lighting", 6144, this, 6, &lt, 1);
    xTaskCreatePinnedToCore(&Controller::housekeeping_entry, "housekeeping", 6144, this, 2, &ht, 0);
    lighting_task_ = lt;
    housekeeping_task_ = ht;
    bus_->post(EventId::SystemBoot, EventPriority::Normal);
    ESP_LOGI(TAG, "boot complete: free heap %lu bytes (min %lu)",
             static_cast<unsigned long>(esp_get_free_heap_size()),
             static_cast<unsigned long>(esp_get_minimum_free_heap_size()));
}

// ---------------------------------------------------------------------------
// Tasks
// ---------------------------------------------------------------------------

void Controller::lighting_entry(void *arg)
{
    static_cast<Controller *>(arg)->lighting_loop();
}

void Controller::housekeeping_entry(void *arg)
{
    static_cast<Controller *>(arg)->housekeeping_loop();
}

void Controller::lighting_loop()
{
    esp_task_wdt_add(nullptr);
    TickType_t last = xTaskGetTickCount();
    InputEvent events[16];
    for (;;) {
        esp_task_wdt_reset();
        int64_t t0 = 0;
        std::size_t n_events = 0;
        {
            ModelLock lock(model_mutex());
            t0 = esp_timer_get_time(); // exclude lock wait from tick timing
            const uint32_t now = uptime_ms();
            lighting_->run_once(now);
            n_events = hardware_->poll_inputs(now, events, 16);
            for (std::size_t i = 0; i < n_events; ++i) {
                const HardwareInputInfo &in = hardware_->inputs()[events[i].index];
                EventPayload payload{};
                payload.data.input.input = events[i].index;
                payload.data.input.active = events[i].state ? 1 : 0;
                bus_->post(EventId::InputStateChanged, EventPriority::Normal, payload);
                ESP_LOGI(TAG, "input %s (%s) -> %s", in.name.c_str(), in.id.c_str(),
                         events[i].state ? "ON" : "off");
                AutoEvent ae;
                ae.type = AutoEventType::kInput;
                ae.id = in.id;
                ae.state = events[i].state;
                automation_->post(ae);
                mqtt_->event("{\"type\":\"input\",\"id\":\"" + in.id + "\",\"name\":\"" + in.name +
                             "\",\"state\":" + (events[i].state ? "true" : "false") + "}");
                ws_->broadcast("input_state_changed",
                               "{\"id\":\"" + in.id + "\",\"name\":\"" + in.name +
                                   "\",\"state\":" + (events[i].state ? "true" : "false") + "}");
            }
            // Every lighting tick (20 ms): event -> action stays well under
            // the 100 ms automation target (SPEC §24).
            automation_->tick(now, auto_host_->clock());
            const uint32_t took = static_cast<uint32_t>(esp_timer_get_time() - t0);
            lighting_->record_tick_us(took, kLightingPeriodMs * 1000);
        }
        vTaskDelayUntil(&last, pdMS_TO_TICKS(kLightingPeriodMs));
    }
}

void Controller::housekeeping_loop()
{
    esp_task_wdt_add(nullptr);
    uint32_t last_net = 0, last_cleanup = 0, last_status = 0, last_ota = 0;
    for (;;) {
        esp_task_wdt_reset();
        const uint32_t now = uptime_ms();

        // Debounced last-state persistence (restore after power loss).
        {
            ModelLock lock(model_mutex());
            if (lighting_->take_state_dirty()) {
                state_pending_ = true;
                state_dirty_since_ms_ = now;
            }
            if (state_pending_ && !config_frozen_ && now - state_dirty_since_ms_ >= kStateSaveDebounceMs) {
                state_pending_ = false;
                if (light_store::save_states(*config_, *lighting_) != ConfigStatus::kOk) {
                    ESP_LOGW(TAG, "light state save failed");
                }
            }
        }
        net_sender_.process();
        mqtt_->poll(now);
        // OTA health check: a new firmware is confirmed only while the web
        // server runs and the lighting loop keeps ticking.
        if (now - last_ota >= 1000) {
            last_ota = now;
            const uint32_t ticks = lighting_->stats().ticks;
            const bool healthy = fs_ok_ && web_->running() && ticks != health_ticks_;
            health_ticks_ = ticks;
            ota_->tick(now, healthy);
        }
        if (now - last_net >= 1000) {
            last_net = now;
            network_->poll();
            if (network_dirty_) {
                network_dirty_ = false;
                cJSON *o = network_status_json(network_->status());
                ws_->broadcast("network_status", api::to_string(o));
                cJSON_Delete(o);
            }
        }
        if (now - last_cleanup >= 60000) {
            last_cleanup = now;
            auth_->cleanup_expired();
            ws_->revalidate_sessions();
        }
        if (now - last_status >= 10000 && ws_->client_count() > 0) {
            last_status = now;
            cJSON *o = cJSON_CreateObject();
            cJSON_AddNumberToObject(o, "uptimeSec", now / 1000);
            cJSON_AddNumberToObject(o, "freeHeap", esp_get_free_heap_size());
            cJSON_AddNumberToObject(o, "minFreeHeap", esp_get_minimum_free_heap_size());
            cJSON_AddNumberToObject(o, "wsClients", ws_->client_count());
            ws_->broadcast("system_status", api::to_string(o));
            cJSON_Delete(o);
        }
        vTaskDelay(pdMS_TO_TICKS(250));
    }
}

// ---------------------------------------------------------------------------
// Light change -> WebSocket
// ---------------------------------------------------------------------------

void Controller::on_light_changed(const Channel &ch, LightChange change)
{
    // Runs in the lighting task with the model lock held: only flag work.
    const std::size_t index = lighting_->channel_manager().index_of(ch.id);
    if (change == LightChange::kConfig) {
        ws_->broadcast("config_changed", "{\"kind\":\"lights\"}");
        if (mqtt_) mqtt_->lights_reconfigured();
        if (espnow_) espnow_->lights_reconfigured();
        return;
    }
    ws_->notify_light(index);
    if (mqtt_) mqtt_->light_changed(index);
    if (control_) control_->light_changed(index);
    if (espnow_) espnow_->light_changed(index, ch.last_origin);
    if (automation_) {
        LightState &prev = last_light_states_[ch.id];
        AutoEvent ae;
        ae.type = AutoEventType::kLight;
        ae.id = ch.id;
        ae.light = ch.requested;
        ae.prev = prev;
        ae.origin = ch.last_origin;
        automation_->post(ae);
        prev = ch.requested;
    }
    EventPayload payload{};
    payload.data.light.channel = static_cast<uint16_t>(index);
    payload.data.light.on = ch.requested.power ? 1 : 0;
    payload.data.light.brightness = ch.requested.brightness;
    payload.data.light.rgb[0] = ch.requested.red;
    payload.data.light.rgb[1] = ch.requested.green;
    payload.data.light.rgb[2] = ch.requested.blue;
    bus_->post(EventId::LightStateChanged, EventPriority::Telemetry, payload);
}

std::string Controller::render_light(std::size_t index)
{
    ModelLock lock(model_mutex());
    const ChannelManager &cm = lighting_->channel_manager();
    if (index >= cm.channel_count()) return std::string();
    const Channel &ch = cm.at(index);
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "channelId", ch.id.c_str());
    cJSON_AddItemToObject(o, "state", light_store::state_to_json(ch.requested));
    cJSON_AddItemToObject(o, "output", light_store::state_to_json(ch.applied));
    cJSON_AddBoolToObject(o, "available", lighting_->hardware_ok(ch.id));
    const std::string effect = lighting_->active_effect(ch.id);
    cJSON_AddStringToObject(o, "effect", effect.c_str());
    const std::string text = api::to_string(o);
    cJSON_Delete(o);
    return text;
}

// ---------------------------------------------------------------------------
// Board profile apply with rollback (SPEC §5.4)
// ---------------------------------------------------------------------------

ProfileApplyOutcome Controller::apply_profile(const std::string &id, bool force)
{
    ProfileApplyOutcome out;
    std::string json;
    if (!boards_->get_json(id, json)) {
        out.message = "Profile '" + id + "' not found";
        out.report.errors.push_back({ValidationReason::kProfileInvalidJson, id, out.message, true});
        out.active_board = boards_->active_id();
        return out;
    }
    BoardProfile candidate;
    bool parse_ok = false;
    out.report = boards_->validate(json, candidate, parse_ok);
    if (!parse_ok || out.report.has_errors()) {
        out.message = "Validation failed - nothing was changed";
        out.active_board = boards_->active_id();
        return out;
    }

    ModelLock lock(model_mutex());
    const BoardProfile previous = hardware_->profile();
    ESP_LOGW(TAG, "applying board profile '%s' (outputs -> safe state)", id.c_str());
    lighting_->detach_hardware();
    hardware_->start(candidate);
    const std::size_t failures = hardware_->failed_count();
    if (failures > 0 && !force) {
        std::string failed;
        for (const auto &h : hardware_->health()) {
            if (!h.ok) failed += (failed.empty() ? "" : ", ") + h.id + " (" + h.detail + ")";
        }
        ESP_LOGE(TAG, "profile '%s' failed diagnostics (%s) - rolling back", id.c_str(), failed.c_str());
        hardware_->start(previous);
        lighting_->attach_hardware(hardware_.get());
        out.rolled_back = true;
        out.message = "Hardware check failed: " + failed + ". The previous profile '" +
                      previous.id + "' was restored.";
        out.active_board = previous.id;
        return out;
    }
    lighting_->attach_hardware(hardware_.get());
    if (boards_->set_active(id) != BoardOpResult::kOk) {
        ESP_LOGW(TAG, "profile applied but could not be persisted");
    }
    out.applied = true;
    out.active_board = id;
    out.message = failures > 0 ? "Applied with " + std::to_string(failures) + " failed peripheral(s) (forced)"
                               : "Profile applied";
    ESP_LOGI(TAG, "%s", out.message.c_str());
    return out;
}

// ---------------------------------------------------------------------------
// Diagnostics, restart, factory reset
// ---------------------------------------------------------------------------

std::string Controller::diagnostics_json()
{
    cJSON *o = cJSON_CreateObject();
    cJSON *heap = cJSON_AddObjectToObject(o, "heap");
    cJSON_AddNumberToObject(heap, "free", esp_get_free_heap_size());
    cJSON_AddNumberToObject(heap, "minFree", esp_get_minimum_free_heap_size());
    cJSON_AddNumberToObject(heap, "largestBlock", heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT));
    cJSON_AddNumberToObject(o, "uptimeSec", static_cast<double>(esp_timer_get_time() / 1000000));
    cJSON_AddStringToObject(o, "resetReason", reset_reason_name(esp_reset_reason()));
    cJSON_AddNumberToObject(o, "bootCount", g_boot_count);
    cJSON_AddStringToObject(o, "firmwareVersion", kVersion);
    {
        esp_chip_info_t chip{};
        esp_chip_info(&chip);
        uint32_t flash = 0;
        esp_flash_get_size(nullptr, &flash);
        cJSON *mcu = cJSON_AddObjectToObject(o, "mcu");
        cJSON_AddStringToObject(mcu, "target", CONFIG_IDF_TARGET);
        cJSON_AddNumberToObject(mcu, "revision", chip.revision);
        cJSON_AddNumberToObject(mcu, "cores", chip.cores);
        cJSON_AddNumberToObject(mcu, "flashBytes", flash);
        cJSON_AddStringToObject(mcu, "idf", esp_get_idf_version());
    }
    {
        // Stack high-water marks of every Lumen task (ARCHITECTURE §3).
        cJSON *stacks = cJSON_AddArrayToObject(o, "taskStacks");
        for (const char *name : {"lighting", "housekeeping", "httpd", "ctrl", "mqtt_task", "tiT", "sys_evt"}) {
            TaskHandle_t t = xTaskGetHandle(name);
            if (t == nullptr) continue;
            cJSON *e = cJSON_CreateObject();
            cJSON_AddStringToObject(e, "name", name);
            cJSON_AddNumberToObject(e, "freeMinBytes", uxTaskGetStackHighWaterMark(t) * sizeof(StackType_t));
            cJSON_AddItemToArray(stacks, e);
        }
    }
    {
        cJSON *a = cJSON_AddObjectToObject(o, "automations");
        int enabled = 0, running = 0;
        for (const auto &au : automation_->automations()) {
            if (au.enabled) ++enabled;
            const AutomationRuntime *rt = automation_->runtime(au.id);
            if (rt != nullptr && rt->running) ++running;
        }
        cJSON_AddNumberToObject(a, "total", static_cast<double>(automation_->automations().size()));
        cJSON_AddNumberToObject(a, "enabled", enabled);
        cJSON_AddNumberToObject(a, "running", running);
        cJSON_AddNumberToObject(a, "droppedEvents", automation_->dropped_events());
    }
    {
        cJSON *errs = cJSON_AddObjectToObject(o, "recentLog");
        cJSON_AddNumberToObject(errs, "warningsAndErrorsSinceBoot", log_capture_total());
        cJSON *lines = cJSON_AddArrayToObject(errs, "lines");
        for (const auto &l : log_capture_recent()) {
            cJSON *e = cJSON_CreateObject();
            cJSON_AddNumberToObject(e, "uptimeSec", l.uptime_s);
            cJSON_AddStringToObject(e, "level", l.level == 'E' ? "error" : "warning");
            cJSON_AddStringToObject(e, "text", l.text.c_str());
            cJSON_AddItemToArray(lines, e);
        }
    }
    {
        // Multi-controller (ESP-NOW, Phase 11) is not implemented yet.
        cJSON *peers = cJSON_AddObjectToObject(o, "peers");
        cJSON_AddBoolToObject(peers, "supported", false);
        cJSON_AddNumberToObject(peers, "count", 0);
    }

    cJSON *tasks = cJSON_AddArrayToObject(o, "tasks");
    auto task = [&](const char *name, void *handle, uint32_t stack) {
        if (handle == nullptr) return;
        cJSON *t = cJSON_CreateObject();
        cJSON_AddStringToObject(t, "name", name);
        cJSON_AddNumberToObject(t, "stackSize", stack);
        cJSON_AddNumberToObject(t, "stackFreeMin",
                                uxTaskGetStackHighWaterMark(static_cast<TaskHandle_t>(handle)));
        cJSON_AddItemToArray(tasks, t);
    };
    task("lighting", lighting_task_, 6144);
    task("housekeeping", housekeeping_task_, 6144);

    {
        ModelLock lock(model_mutex());
        const LightingStats &ls = lighting_->stats();
        cJSON *l = cJSON_AddObjectToObject(o, "lighting");
        cJSON_AddNumberToObject(l, "lights", lighting_->channel_manager().channel_count());
        cJSON_AddNumberToObject(l, "commandsProcessed", ls.commands_processed);
        cJSON_AddNumberToObject(l, "commandsRejected", ls.commands_rejected);
        cJSON_AddNumberToObject(l, "queueDropped", ls.queue_dropped);
        cJSON_AddNumberToObject(l, "queueDepth", lighting_->queue_depth());
        cJSON_AddNumberToObject(l, "hardwareWrites", ls.hardware_writes);
        cJSON_AddNumberToObject(l, "hardwareErrors", ls.hardware_errors);
        cJSON_AddNumberToObject(l, "ticks", ls.ticks);
        cJSON_AddNumberToObject(l, "maxTickUs", ls.max_tick_us);
        cJSON_AddNumberToObject(l, "tickOverruns", ls.tick_overruns);
        cJSON *lat = cJSON_AddObjectToObject(l, "commandLatencyUs");
        cJSON_AddNumberToObject(lat, "last", ls.cmd_latency_last_us);
        cJSON_AddNumberToObject(lat, "avg", ls.cmd_latency_count ? static_cast<double>(ls.cmd_latency_sum_us / ls.cmd_latency_count) : 0);
        cJSON_AddNumberToObject(lat, "max", ls.cmd_latency_max_us);
        cJSON_AddNumberToObject(lat, "count", ls.cmd_latency_count);
        cJSON *al = cJSON_AddObjectToObject(l, "automationEventWaitUs");
        cJSON_AddNumberToObject(al, "last", automation_->event_latency_last_us());
        cJSON_AddNumberToObject(al, "max", automation_->event_latency_max_us());
        cJSON *hw = cJSON_AddArrayToObject(o, "peripherals");
        for (const auto &h : hardware_->health()) {
            cJSON *e = cJSON_CreateObject();
            cJSON_AddStringToObject(e, "id", h.id.c_str());
            cJSON_AddStringToObject(e, "type", h.type.c_str());
            cJSON_AddBoolToObject(e, "ok", h.ok);
            cJSON_AddNumberToObject(e, "errors", h.errors);
            cJSON_AddStringToObject(e, "detail", h.detail.c_str());
            cJSON_AddItemToArray(hw, e);
        }
    }
    const EventBusStats &bs = bus_->stats();
    cJSON *b = cJSON_AddObjectToObject(o, "eventBus");
    cJSON_AddNumberToObject(b, "posted", bs.posted);
    cJSON_AddNumberToObject(b, "delivered", bs.poll_delivered);
    cJSON_AddNumberToObject(b, "droppedTelemetry", bs.dropped[0]);
    cJSON_AddNumberToObject(b, "droppedNormal", bs.dropped[1]);
    cJSON_AddNumberToObject(b, "droppedCritical", bs.dropped[2]);
    cJSON_AddNumberToObject(b, "coalesced", bs.coalesced);
    cJSON *w = cJSON_AddObjectToObject(o, "web");
    cJSON_AddNumberToObject(w, "requests", web_->requests_served());
    cJSON_AddNumberToObject(w, "wsClients", ws_->client_count());
    cJSON_AddNumberToObject(w, "wsSent", ws_->sent());
    cJSON_AddNumberToObject(w, "wsDropped", ws_->dropped());
    cJSON_AddNumberToObject(w, "sessions", auth_->session_count());
    FsUsage fs;
    platform_fs_usage(fs);
    cJSON *f = cJSON_AddObjectToObject(o, "storage");
    cJSON_AddBoolToObject(f, "mounted", fs_ok_);
    cJSON_AddNumberToObject(f, "total", static_cast<double>(fs.total));
    cJSON_AddNumberToObject(f, "used", static_cast<double>(fs.used));
    cJSON_AddItemToObject(o, "network", network_status_json(network_->status()));
    const std::string text = api::to_string(o);
    cJSON_Delete(o);
    return text;
}

void Controller::load_location()
{
    std::string text;
    if (config_->load_document("automation.json", text) != ConfigStatus::kOk) return;
    cJSON *root = cJSON_Parse(text.c_str());
    const cJSON *lat = root ? cJSON_GetObjectItemCaseSensitive(root, "latitude") : nullptr;
    const cJSON *lon = root ? cJSON_GetObjectItemCaseSensitive(root, "longitude") : nullptr;
    if (cJSON_IsNumber(lat) && cJSON_IsNumber(lon)) {
        auto_host_->set_location(lat->valuedouble, lon->valuedouble);
    }
    cJSON_Delete(root);
}

bool Controller::save_location(double lat, double lon)
{
    if (lat < -90 || lat > 90 || lon < -180 || lon > 180) return false;
    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "latitude", lat);
    cJSON_AddNumberToObject(root, "longitude", lon);
    const std::string text = api::to_string(root);
    cJSON_Delete(root);
    if (config_->save_document("automation.json", text) != ConfigStatus::kOk) return false;
    auto_host_->set_location(lat, lon);
    return true;
}

void Controller::on_shutdown()
{
    if (!config_frozen_) {
        ModelLock lock(model_mutex());
        if (lighting_->take_state_dirty() || state_pending_) {
            state_pending_ = false;
            if (light_store::save_states(*config_, *lighting_) != ConfigStatus::kOk) {
                ESP_LOGW(TAG, "light state save at shutdown failed");
            }
        }
    }
    if (mqtt_) mqtt_->stop();
}

void Controller::schedule_restart(uint32_t delay_ms)
{
    ESP_LOGW(TAG, "restart requested (in %lu ms)", static_cast<unsigned long>(delay_ms));
    xTaskCreate(
        [](void *arg) {
            vTaskDelay(pdMS_TO_TICKS(reinterpret_cast<uintptr_t>(arg)));
            // Outputs: the PCA9685 is re-initialised to all-off at boot before
            // the startup behaviour is applied, relays reset with the SoC.
            esp_restart();
        },
        "restart", 6144, reinterpret_cast<void *>(static_cast<uintptr_t>(delay_ms)), 1, nullptr);
}

void Controller::factory_reset()
{
    ESP_LOGW(TAG, "FACTORY RESET: erasing configuration and credentials");
    {
        ModelLock lock(model_mutex());
        lighting_->detach_hardware();
        config_frozen_ = true;
        remove_tree(kConfigRoot);
        mkdir(kConfigRoot, 0755);
    }
    nvs_flash_deinit();
    nvs_flash_erase(); // credentials, network settings, admin password
    schedule_restart(1500);
}

} // namespace lumen
