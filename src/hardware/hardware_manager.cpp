// Lumen — hardware manager implementation. See hardware_manager.h
#include "lumen/hardware/hardware_manager.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "core/cJSON.h"

#ifdef ESP_PLATFORM
#include "esp_log.h"
#define HW_LOGI(...) ESP_LOGI("hw", __VA_ARGS__)
#define HW_LOGW(...) ESP_LOGW("hw", __VA_ARGS__)
#else
#define HW_LOGI(...) ((void)0)
#define HW_LOGW(...) ((void)0)
#endif

namespace lumen
{

namespace
{

constexpr uint32_t kGpioDebounceMs = 30;

const char *err_name(esp_err_t err)
{
#ifdef ESP_PLATFORM
    return esp_err_to_name(err);
#else
    return err == ESP_OK ? "ESP_OK" : "error";
#endif
}

// Reads label settings from a peripheral's raw "settings" JSON:
//   {"label_prefix": "DI", "label_start": 9}
void input_label_settings(const BoardPeripheral &p, std::string &prefix, int &start)
{
    prefix.clear();
    start = 1;
    if (p.settings_json.empty()) {
        return;
    }
    cJSON *root = cJSON_Parse(p.settings_json.c_str());
    if (root == nullptr) {
        return;
    }
    cJSON *pre = cJSON_GetObjectItemCaseSensitive(root, "label_prefix");
    if (cJSON_IsString(pre) && pre->valuestring != nullptr) {
        prefix = pre->valuestring;
    }
    cJSON *st = cJSON_GetObjectItemCaseSensitive(root, "label_start");
    if (cJSON_IsNumber(st)) {
        start = st->valueint;
    }
    cJSON_Delete(root);
}

bool resource_is_relay_bus(const BoardProfile &profile, const std::string &res_id)
{
    for (const auto &p : profile.peripherals) {
        if (p.type == "relay" && p.bus == res_id) {
            return true;
        }
    }
    return false;
}

} // namespace

HardwareManager::HardwareManager(IDriverFactory &factory) : factory_(factory) {}

HardwareManager::~HardwareManager()
{
    stop();
}

HardwareManager::DriverEntry *HardwareManager::find_entry(const std::string &id)
{
    for (auto &e : drivers_) {
        if (e.id == id) {
            return &e;
        }
    }
    return nullptr;
}

const HardwareManager::DriverEntry *HardwareManager::find_entry(const std::string &id) const
{
    for (const auto &e : drivers_) {
        if (e.id == id) {
            return &e;
        }
    }
    return nullptr;
}

esp_err_t HardwareManager::start(const BoardProfile &profile)
{
    stop();
    profile_ = profile;
    BoardProfileIndex index;
    index.index(profile_);

    // 1. Buses first (peripherals depend on them).
    for (const auto &res : profile_.resources) {
        if (res.type != "i2c") {
            continue;
        }
        DriverEntry e;
        e.id = res.id;
        e.type = "i2c";
        std::unique_ptr<II2CBus> bus = factory_.create_i2c(res);
        if (bus) {
            e.init_err = bus->initialize();
            e.owned = std::move(bus);
            e.driver = (e.init_err == ESP_OK) ? e.owned.get() : nullptr;
        } else {
            e.init_err = ESP_ERR_NOT_SUPPORTED;
        }
        if (e.init_err != ESP_OK) {
            HW_LOGW("i2c '%s' failed: %s", res.id.c_str(), err_name(e.init_err));
        }
        drivers_.push_back(std::move(e));
    }

    // 2. Peripherals.
    for (const auto &p : profile_.peripherals) {
        if (p.type == "pca9685" || p.type == "pcf8574") {
            DriverEntry *bus_entry = find_entry(p.bus);
            II2CBus *bus = (bus_entry != nullptr && bus_entry->driver != nullptr)
                               ? static_cast<II2CBus *>(bus_entry->driver)
                               : nullptr;
            if (p.type == "pca9685") {
                DriverEntry e;
                e.id = p.id;
                e.type = "pca9685";
                e.channels = p.channels > 0 ? p.channels : 16;
                if (bus == nullptr || p.address < 0) {
                    e.init_err = ESP_ERR_INVALID_STATE;
                } else {
                    auto drv = std::make_unique<Pca9685Driver>(
                        *bus, static_cast<uint8_t>(p.address),
                        p.frequency > 0 ? static_cast<uint32_t>(p.frequency) : 1000U);
                    e.init_err = drv->initialize();
                    e.owned = std::move(drv);
                    e.driver = (e.init_err == ESP_OK) ? e.owned.get() : nullptr;
                }
                if (e.init_err != ESP_OK) {
                    HW_LOGW("pca9685 '%s' failed: %s", p.id.c_str(), err_name(e.init_err));
                } else {
                    HW_LOGI("pca9685 '%s' @0x%02lx ready (%d ch)", p.id.c_str(),
                            static_cast<unsigned long>(p.address), e.channels);
                }
                drivers_.push_back(std::move(e));
            } else {
                PcfEntry pe;
                pe.id = p.id;
                pe.channels = (p.channels > 0 && p.channels <= 8) ? p.channels : 8;
                pe.first_input = inputs_.size();
                std::string prefix;
                int start = 1;
                input_label_settings(p, prefix, start);
                for (int pin = 0; pin < pe.channels && inputs_.size() < kMaxInputs; ++pin) {
                    HardwareInputInfo info;
                    info.id = p.id + ":" + std::to_string(pin);
                    info.name = prefix.empty() ? (p.id + " #" + std::to_string(pin + 1))
                                               : (prefix + std::to_string(start + pin));
                    info.source = "pcf8574";
                    inputs_.push_back(info);
                }
                if (bus == nullptr || p.address < 0) {
                    pe.init_err = ESP_ERR_INVALID_STATE;
                } else {
                    pe.driver = std::make_unique<Pcf8574Driver>(
                        *bus, static_cast<uint8_t>(p.address),
                        static_cast<uint8_t>(pcfs_.size()), p.active_low);
                    pe.init_err = pe.driver->initialize();
                }
                for (int pin = 0; pin < pe.channels; ++pin) {
                    const std::size_t idx = pe.first_input + static_cast<std::size_t>(pin);
                    if (idx >= inputs_.size()) {
                        break;
                    }
                    inputs_[idx].healthy = (pe.init_err == ESP_OK);
                    bool level = false;
                    if (pe.init_err == ESP_OK &&
                        pe.driver->get_level(static_cast<uint8_t>(pin), level) == ESP_OK) {
                        inputs_[idx].state = level;
                    }
                }
                if (pe.init_err != ESP_OK) {
                    HW_LOGW("pcf8574 '%s' failed: %s", p.id.c_str(), err_name(pe.init_err));
                }
                pcfs_.push_back(std::move(pe));
            }
        } else if (p.type == "relay") {
            DriverEntry e;
            e.id = p.id;
            e.type = "relay";
            const BoardResource *res = index.find_resource(p.bus);
            if (res == nullptr || res->gpio < 0) {
                e.init_err = ESP_ERR_INVALID_ARG;
            } else {
                std::unique_ptr<IRelayDriver> relay = factory_.create_relay(*res, p.active_low);
                if (relay) {
                    e.init_err = relay->initialize();
                    e.owned = std::move(relay);
                    e.driver = (e.init_err == ESP_OK) ? e.owned.get() : nullptr;
                } else {
                    e.init_err = ESP_ERR_NOT_SUPPORTED;
                }
            }
            if (e.init_err != ESP_OK) {
                HW_LOGW("relay '%s' failed: %s", p.id.c_str(), err_name(e.init_err));
            }
            drivers_.push_back(std::move(e));
        }
        // ethernet / rs485 / addressable_led are owned by their services.
    }

    // 3. Native PWM channels and free GPIOs.
    for (const auto &res : profile_.resources) {
        if (res.type == "pwm") {
            DriverEntry e;
            e.id = res.id;
            e.type = "ledc";
            std::unique_ptr<IPwmDriver> pwm = factory_.create_pwm(res);
            if (pwm) {
                e.init_err = pwm->initialize();
                e.owned = std::move(pwm);
                e.driver = (e.init_err == ESP_OK) ? e.owned.get() : nullptr;
            } else {
                e.init_err = ESP_ERR_NOT_SUPPORTED;
            }
            if (e.init_err != ESP_OK) {
                HW_LOGW("pwm '%s' failed: %s", res.id.c_str(), err_name(e.init_err));
            }
            drivers_.push_back(std::move(e));
        } else if (res.type == "gpio" && res.gpio >= 0) {
            if (res.direction == "output" && !resource_is_relay_bus(profile_, res.id)) {
                DriverEntry e;
                e.id = res.id;
                e.type = "gpio";
                std::unique_ptr<IRelayDriver> out = factory_.create_relay(res, res.inverted);
                if (out) {
                    e.init_err = out->initialize();
                    e.owned = std::move(out);
                    e.driver = (e.init_err == ESP_OK) ? e.owned.get() : nullptr;
                } else {
                    e.init_err = ESP_ERR_NOT_SUPPORTED;
                }
                drivers_.push_back(std::move(e));
            } else if (res.direction == "input" && inputs_.size() < kMaxInputs) {
                GpioInputEntry ge;
                ge.input_index = inputs_.size();
                HardwareInputInfo info;
                info.id = res.id;
                info.name = res.id;
                info.source = "gpio";
                ge.driver = factory_.create_input(res);
                ge.init_err = ge.driver ? ge.driver->initialize() : ESP_ERR_NOT_SUPPORTED;
                info.healthy = (ge.init_err == ESP_OK);
                bool level = false;
                if (ge.init_err == ESP_OK && ge.driver->read(level) == ESP_OK) {
                    ge.stable = level;
                    info.state = level;
                }
                inputs_.push_back(info);
                gpio_inputs_.push_back(std::move(ge));
            }
        }
    }

    running_ = true;
    HW_LOGI("profile '%s' started: %u drivers, %u inputs, %u failed",
            profile_.id.c_str(), static_cast<unsigned>(drivers_.size()),
            static_cast<unsigned>(inputs_.size()), static_cast<unsigned>(failed_count()));
    return ESP_OK;
}

void HardwareManager::stop()
{
    // Reverse order: outputs/peripherals before the buses they sit on.
    for (auto it = drivers_.rbegin(); it != drivers_.rend(); ++it) {
        if (it->type != "i2c" && it->owned) {
            it->owned->shutdown();
        }
    }
    gpio_inputs_.clear();
    pcfs_.clear();
    for (auto it = drivers_.rbegin(); it != drivers_.rend(); ++it) {
        if (it->type == "i2c" && it->owned) {
            it->owned->shutdown();
        }
    }
    drivers_.clear();
    inputs_.clear();
    running_ = false;
}

IDriver *HardwareManager::driver(const std::string &id)
{
    DriverEntry *e = find_entry(id);
    return e != nullptr ? e->driver : nullptr;
}

esp_err_t HardwareManager::flush_outputs()
{
    esp_err_t result = ESP_OK;
    for (auto &e : drivers_) {
        if (e.type == "pca9685" && e.driver != nullptr) {
            esp_err_t err = static_cast<Pca9685Driver *>(e.driver)->flush();
            if (err != ESP_OK) {
                result = err;
            }
        }
    }
    return result;
}

II2CBus *HardwareManager::i2c_bus(const std::string &resource_id)
{
    for (auto &e : drivers_) {
        if (e.type == "i2c" && e.driver != nullptr &&
            (resource_id.empty() || e.id == resource_id)) {
            return static_cast<II2CBus *>(e.driver);
        }
    }
    return nullptr;
}

std::size_t HardwareManager::poll_inputs(uint32_t now_ms, InputEvent *events,
                                         std::size_t max_events)
{
    std::size_t count = 0;
    for (auto &pe : pcfs_) {
        if (!pe.driver) {
            continue;
        }
        if (pe.init_err != ESP_OK) {
            // Periodic recovery attempt: re-release the pins.
            pe.init_err = pe.driver->initialize();
            if (pe.init_err != ESP_OK) {
                continue;
            }
        }
        InputChangeEvent changes[Pcf8574Driver::kPinCount];
        std::size_t n = 0;
        const esp_err_t err = pe.driver->poll(now_ms, changes, Pcf8574Driver::kPinCount, n);
        const bool healthy = (err == ESP_OK);
        for (int pin = 0; pin < pe.channels; ++pin) {
            const std::size_t idx = pe.first_input + static_cast<std::size_t>(pin);
            if (idx < inputs_.size()) {
                inputs_[idx].healthy = healthy;
            }
        }
        for (std::size_t i = 0; i < n; ++i) {
            const std::size_t idx = pe.first_input + changes[i].pin;
            if (idx >= inputs_.size() || changes[i].pin >= pe.channels) {
                continue;
            }
            inputs_[idx].state = changes[i].level;
            inputs_[idx].changed_ms = now_ms;
            if (count < max_events) {
                events[count++] = {static_cast<uint16_t>(idx), changes[i].level, now_ms};
            }
        }
    }
    for (auto &ge : gpio_inputs_) {
        if (!ge.driver || ge.init_err != ESP_OK) {
            continue;
        }
        bool level = false;
        if (ge.driver->read(level) != ESP_OK) {
            continue;
        }
        if (level == ge.stable) {
            ge.pending = false;
            continue;
        }
        if (!ge.pending) {
            ge.pending = true;
            ge.since_ms = now_ms;
        }
        if (now_ms - ge.since_ms >= kGpioDebounceMs) {
            ge.pending = false;
            ge.stable = level;
            inputs_[ge.input_index].state = level;
            inputs_[ge.input_index].changed_ms = now_ms;
            if (count < max_events) {
                events[count++] = {static_cast<uint16_t>(ge.input_index), level, now_ms};
            }
        }
    }
    return count;
}

std::vector<HardwareOutputInfo> HardwareManager::outputs() const
{
    std::vector<HardwareOutputInfo> out;
    for (const auto &e : drivers_) {
        if (e.type == "pca9685") {
            for (int ch = 0; ch < e.channels; ++ch) {
                HardwareOutputInfo o;
                o.id = e.id + ":" + std::to_string(ch);
                o.name = "PWM" + std::to_string(ch + 1);
                o.driver = "pca9685";
                o.device = e.id;
                o.channel = ch;
                if (e.driver != nullptr) {
                    PwmDuty d = 0;
                    static_cast<Pca9685Driver *>(e.driver)->get_channel_duty(
                        static_cast<uint8_t>(ch), d);
                    o.level = static_cast<uint16_t>((static_cast<uint32_t>(d) * 65535U) / 4095U);
                    o.on = d > 0;
                }
                out.push_back(o);
            }
        } else if (e.type == "relay" || e.type == "gpio") {
            HardwareOutputInfo o;
            o.id = e.id;
            o.name = e.id;
            o.driver = e.type;
            o.device = e.id;
            if (e.driver != nullptr) {
                uint32_t v = 0;
                static_cast<IRelayDriver *>(e.driver)->get_value(v);
                o.on = v != 0;
                o.level = o.on ? 65535 : 0;
            }
            out.push_back(o);
        } else if (e.type == "ledc") {
            HardwareOutputInfo o;
            o.id = e.id;
            o.name = e.id;
            o.driver = "ledc";
            o.device = e.id;
            if (e.driver != nullptr) {
                PwmDuty d = 0;
                static_cast<IPwmDriver *>(e.driver)->get_duty(d);
                o.level = d;
                o.on = d > 0;
            }
            out.push_back(o);
        }
    }
    return out;
}

std::vector<PeripheralHealth> HardwareManager::health() const
{
    std::vector<PeripheralHealth> out;
    for (const auto &e : drivers_) {
        PeripheralHealth h;
        h.id = e.id;
        h.type = e.type;
        h.ok = e.driver != nullptr;
        h.detail = h.ok ? "ok" : err_name(e.init_err);
        if (e.type == "pca9685" && e.driver != nullptr) {
            const auto *pca = static_cast<const Pca9685Driver *>(e.driver);
            h.ok = pca->online();
            h.errors = pca->error_count();
            h.detail = h.ok ? (pca->recovery_count() > 0 ? "ok (recovered " + std::to_string(pca->recovery_count()) + "x)" : "ok") : "not responding";
        }
        out.push_back(h);
    }
    for (const auto &pe : pcfs_) {
        PeripheralHealth h;
        h.id = pe.id;
        h.type = "pcf8574";
        h.ok = pe.driver && pe.init_err == ESP_OK && pe.driver->healthy();
        h.errors = pe.driver ? pe.driver->read_failure_count() : 0;
        h.detail = h.ok ? "ok" : err_name(pe.init_err != ESP_OK ? pe.init_err : ESP_FAIL);
        out.push_back(h);
    }
    for (const auto &ge : gpio_inputs_) {
        PeripheralHealth h;
        h.id = inputs_[ge.input_index].id;
        h.type = "gpio_input";
        h.ok = ge.init_err == ESP_OK;
        h.detail = h.ok ? "ok" : err_name(ge.init_err);
        out.push_back(h);
    }
    return out;
}

esp_err_t HardwareManager::set_output(const std::string &output_id, uint16_t level)
{
    const std::size_t colon = output_id.find(':');
    const std::string device = output_id.substr(0, colon);
    DriverEntry *e = find_entry(device);
    if (e == nullptr) {
        return ESP_ERR_NOT_FOUND;
    }
    if (e->driver == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }
    if (e->type == "pca9685") {
        if (colon == std::string::npos) {
            return ESP_ERR_INVALID_ARG;
        }
        const int ch = std::atoi(output_id.c_str() + colon + 1);
        if (ch < 0 || ch >= e->channels) {
            return ESP_ERR_INVALID_ARG;
        }
        auto *pca = static_cast<Pca9685Driver *>(e->driver);
        esp_err_t err = pca->set_channel_duty(static_cast<uint8_t>(ch),
                                              static_cast<PwmDuty>(level >> 4));
        return err == ESP_OK ? pca->flush() : err;
    }
    if (e->type == "relay" || e->type == "gpio") {
        return static_cast<IRelayDriver *>(e->driver)->set_value(level != 0 ? 1 : 0);
    }
    if (e->type == "ledc") {
        return static_cast<IPwmDriver *>(e->driver)->set_duty(level);
    }
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t HardwareManager::read_output(const std::string &output_id, uint16_t &level)
{
    const std::size_t colon = output_id.find(':');
    DriverEntry *e = find_entry(output_id.substr(0, colon));
    if (e == nullptr) return ESP_ERR_NOT_FOUND;
    if (e->driver == nullptr) return ESP_ERR_INVALID_STATE;
    if (e->type == "pca9685") {
        if (colon == std::string::npos) return ESP_ERR_INVALID_ARG;
        const int ch = std::atoi(output_id.c_str() + colon + 1);
        uint16_t on = 0, off = 0, duty = 0;
        const esp_err_t err =
            static_cast<Pca9685Driver *>(e->driver)->read_back(static_cast<uint8_t>(ch), on, off, duty);
        level = static_cast<uint16_t>((static_cast<uint32_t>(duty) * 65535U) / 4095U);
        return err;
    }
    if (e->type == "relay" || e->type == "gpio") {
        uint32_t v = 0;
        static_cast<IRelayDriver *>(e->driver)->get_value(v);
        level = v ? 65535 : 0;
        return ESP_OK;
    }
    if (e->type == "ledc") {
        PwmDuty d = 0;
        static_cast<IPwmDriver *>(e->driver)->get_duty(d);
        level = d;
        return ESP_OK;
    }
    return ESP_ERR_NOT_SUPPORTED;
}

std::size_t HardwareManager::failed_count() const
{
    std::size_t n = 0;
    for (const auto &e : drivers_) {
        if (e.driver == nullptr) {
            ++n;
        }
    }
    for (const auto &pe : pcfs_) {
        if (pe.init_err != ESP_OK) {
            ++n;
        }
    }
    for (const auto &ge : gpio_inputs_) {
        if (ge.init_err != ESP_OK) {
            ++n;
        }
    }
    return n;
}

} // namespace lumen
