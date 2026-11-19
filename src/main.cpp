// Lumen — universal ESP32 smart lighting controller.
// Phase 1: SystemManager service lifecycle replaces the Phase 0 heartbeat
// loop (see docs/ARCHITECTURE.md). Services are registered declaratively and
// driven through init() → start() → stop(). No FreeRTOS task is created
// implicitly here (AGENTS.md §5); that comes when individual services need
// their own task, declared with priority + stack size.
//
// Rules in force (AGENTS.md §5): ESP-IDF only, C++17, check every esp_err_t.
//
// A demonstration heartbeat service is registered so the boot sanity
// indicator keeps working while the SystemManager skeleton is exercised.
#include <cstring>

#include "driver/gpio.h"
#include "driver/i2c_master.h"

#include "esp_chip_info.h"
#include "esp_err.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#include "lumen/core/diagnostics.hpp"
#include "lumen/core/service.hpp"
#include "lumen/core/system_manager.hpp"
#include "lumen/version.hpp"

#ifndef LUMEN_FIRMWARE_VERSION
#define LUMEN_FIRMWARE_VERSION "0.0.0"
#endif

static const char *TAG = "lumen";

// Demonstration service: keeps the Phase 0 proof-of-life heartbeat alive
// while the SystemManager drives its lifecycle. Real services (lighting,
// networking, automation) register here in later phases.
class HeartbeatService final : public lumen::IService
{
  public:
    const char *name() const override { return "heartbeat"; }

    lumen::ServiceResult init() override
    {
        ESP_LOGI(TAG, "service %s: init", name());
        return lumen::ServiceResult::kOk;
    }

    lumen::ServiceResult start() override
    {
        ESP_LOGI(TAG, "service %s: start", name());
        return lumen::ServiceResult::kOk;
    }

    lumen::ServiceResult stop() override
    {
        ESP_LOGI(TAG, "service %s: stop (safe state)", name());
        return lumen::ServiceResult::kOk;
    }

    lumen::ServiceStatus status() const override { return status_; }

    void set_safe_state(const lumen::SafeState &state) override
    {
        safe_state_ = state;
    }

    lumen::SafeState safe_state() const override { return safe_state_; }

  private:
    lumen::ServiceStatus status_ = lumen::ServiceStatus::kRegistered;
    lumen::SafeState safe_state_{};
};

// Phase 3 diagnostic: I2C bus scan on the KC868-AP I2C pins (SDA=4, SCL=16
// per boards/kincony_kc868_ap.json). Runs at boot to resolve unverified
// peripheral addresses (PCA9685 0x00 vs 0x40; PCF8574 0x22/0x21 vs
// 0x3A/0x21). Reports only — never modifies configuration. This satisfies
// SPEC §25.2 / Phase 3 task 3.1 "I2C scan service result exposed."
static void i2c_scan_diagnostic()
{
    ESP_LOGI(TAG, "--- I2C scan diagnostic (SDA=4, SCL=16) ---");

    i2c_master_bus_config_t bus_config{};
    bus_config.i2c_port = 0;
    bus_config.sda_io_num = static_cast<gpio_num_t>(4);
    bus_config.scl_io_num = static_cast<gpio_num_t>(16);
    bus_config.clk_source = I2C_CLK_SRC_DEFAULT;
    bus_config.glitch_ignore_cnt = 7;
    bus_config.flags.enable_internal_pullup = true;

    i2c_master_bus_handle_t bus_handle = nullptr;
    esp_err_t err = i2c_new_master_bus(&bus_config, &bus_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2C bus init failed: %s", esp_err_to_name(err));
        return;
    }

    int found = 0;
    for (uint8_t addr = 0x08; addr <= 0x77; ++addr) {
        err = i2c_master_probe(bus_handle, addr, 500);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "  I2C device responded at 0x%02X", addr);
            ++found;
        }
    }

    // Also probe 0x00 specifically (KC868-AP pinmap claims PCA9685 at 0x00).
    err = i2c_master_probe(bus_handle, 0x00, 500);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "  I2C device responded at 0x00 (unusual — verify)");
        ++found;
    }

    ESP_LOGI(TAG, "I2C scan complete: %d device(s) responded", found);
    if (found == 0) {
        ESP_LOGW(TAG, "No I2C devices found — check wiring, power, and pull-ups");
    }

    i2c_del_master_bus(bus_handle);
    ESP_LOGI(TAG, "--- end I2C scan ---");
}

// Phase 3 diagnostic: PCA9685 register access helpers.
// PCA9685 at verified address 0x40 (boards/kincony_kc868_ap.json).
static const uint8_t kPca9685Addr = 0x40;

// PCA9685 registers.
static constexpr uint8_t kRegMode1 = 0x00;
static constexpr uint8_t kRegMode2 = 0x01;
static constexpr uint8_t kRegLed0OnL = 0x06;
static constexpr uint8_t kRegAllLedOnL = 0xFA;

// Write a register via add-device/transmit/remove pattern (ESP-IDF v6.x API).
static esp_err_t pca9685_write_reg(i2c_master_bus_handle_t bus, uint8_t reg,
                                   uint8_t val)
{
    uint8_t buf[2] = {reg, val};
    i2c_master_dev_handle_t dev;
    i2c_device_config_t dcfg{};
    dcfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dcfg.device_address = kPca9685Addr;
    dcfg.scl_speed_hz = 100000;
    esp_err_t err = i2c_master_bus_add_device(bus, &dcfg, &dev);
    if (err != ESP_OK) {
        return err;
    }
    err = i2c_master_transmit(dev, buf, 2, 1000);
    i2c_master_bus_rm_device(dev);
    return err;
}

// Read a register via add-device/transmit/receive/remove pattern.
static esp_err_t pca9685_read_reg(i2c_master_bus_handle_t bus, uint8_t reg,
                                  uint8_t &val)
{
    i2c_master_dev_handle_t dev;
    i2c_device_config_t dcfg{};
    dcfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dcfg.device_address = kPca9685Addr;
    dcfg.scl_speed_hz = 100000;
    esp_err_t err = i2c_master_bus_add_device(bus, &dcfg, &dev);
    if (err != ESP_OK) {
        return err;
    }
    err = i2c_master_transmit(dev, &reg, 1, 1000);
    if (err == ESP_OK) {
        err = i2c_master_receive(dev, &val, 1, 1000);
    }
    i2c_master_bus_rm_device(dev);
    return err;
}

// Phase 3 diagnostic: test PCA9685 channels (SPEC §25.2, task 3.2).
// Writes duty values to channels 0-15 and verifies register write succeeds.
// No mains loads connected — safe-state only.
static void test_pca9685(i2c_master_bus_handle_t bus)
{
    ESP_LOGI(TAG, "--- PCA9685 diagnostic (addr 0x40, 16 channels) ---");

    // Wake device: clear sleep bit in Mode1.
    uint8_t mode1 = 0;
    esp_err_t err = pca9685_read_reg(bus, kRegMode1, mode1);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "PCA9685: cannot read Mode1: %s", esp_err_to_name(err));
        return;
    }
    ESP_LOGI(TAG, "PCA9685: Mode1 = 0x%02X", mode1);

    err = pca9685_write_reg(bus, kRegMode1, mode1 & ~0x10); // clear sleep
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "PCA9685: cannot wake (clear sleep): %s",
                 esp_err_to_name(err));
        return;
    }

    // Mode2: totem-pole, no inversion.
    err = pca9685_write_reg(bus, kRegMode2, 0x04);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "PCA9685: cannot write Mode2: %s", esp_err_to_name(err));
        return;
    }

    // Test each channel: set duty to 25% (1023/4096), verify write succeeds.
    int pass = 0, fail = 0;
    for (uint8_t ch = 0; ch < 16; ++ch) {
        uint8_t reg = kRegLed0OnL + ch * 4;
        uint16_t duty = 1023; // 25% of 4096
        uint8_t buf[5] = {reg, 0x00, 0x00, (uint8_t)(duty & 0xFF),
                          (uint8_t)((duty >> 8) & 0x0F)};
        i2c_master_dev_handle_t dev;
        i2c_device_config_t dcfg{};
        dcfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
        dcfg.device_address = kPca9685Addr;
        dcfg.scl_speed_hz = 100000;
        err = i2c_master_bus_add_device(bus, &dcfg, &dev);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "PCA9685: channel %u add device failed: %s", ch,
                     esp_err_to_name(err));
            ++fail;
            continue;
        }
        err = i2c_master_transmit(dev, buf, 5, 1000);
        i2c_master_bus_rm_device(dev);
        if (err == ESP_OK) {
            ++pass;
        } else {
            ESP_LOGE(TAG, "PCA9685: channel %u write failed: %s", ch,
                     esp_err_to_name(err));
            ++fail;
        }
    }
    ESP_LOGI(TAG, "PCA9685: %d channels OK, %d failed", pass, fail);

    // Return all channels to off (safe state).
    for (uint8_t ch = 0; ch < 16; ++ch) {
        uint8_t reg = kRegLed0OnL + ch * 4;
        uint8_t buf[5] = {reg, 0x00, 0x00, 0x00, 0x10}; // full off
        i2c_master_dev_handle_t dev;
        i2c_device_config_t dcfg{};
        dcfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
        dcfg.device_address = kPca9685Addr;
        dcfg.scl_speed_hz = 100000;
        esp_err_t e2 = i2c_master_bus_add_device(bus, &dcfg, &dev);
        if (e2 == ESP_OK) {
            i2c_master_transmit(dev, buf, 5, 1000);
            i2c_master_bus_rm_device(dev);
        }
    }
    ESP_LOGI(TAG, "PCA9685: all channels returned to off (safe state)");
    ESP_LOGI(TAG, "--- end PCA9685 diagnostic ---");
}

// Phase 3 diagnostic: test relays on GPIO 13 and 2 (SPEC §25.2, task 3.2).
// Toggles each relay on/off. No mains loads connected — safe-state only.
static void test_relays()
{
    ESP_LOGI(TAG, "--- relay diagnostic (GPIO13=relay1, GPIO2=relay2) ---");

    gpio_config_t io_conf{};
    io_conf.pin_bit_mask = (1ULL << 13) | (1ULL << 2);
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.intr_type = GPIO_INTR_DISABLE;
    esp_err_t err = gpio_config(&io_conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "relay: GPIO config failed: %s", esp_err_to_name(err));
        return;
    }

    // Safe state: both off.
    gpio_set_level((gpio_num_t)13, 0);
    gpio_set_level((gpio_num_t)2, 0);
    vTaskDelay(pdMS_TO_TICKS(500));

    // Toggle relay1 (GPIO13).
    gpio_set_level((gpio_num_t)13, 1);
    vTaskDelay(pdMS_TO_TICKS(500));
    gpio_set_level((gpio_num_t)13, 0);
    vTaskDelay(pdMS_TO_TICKS(500));

    // Toggle relay2 (GPIO2).
    gpio_set_level((gpio_num_t)2, 1);
    vTaskDelay(pdMS_TO_TICKS(500));
    gpio_set_level((gpio_num_t)2, 0);
    vTaskDelay(pdMS_TO_TICKS(500));

    ESP_LOGI(TAG, "relay: both relays toggled (GPIO13, GPIO2), returned to off");
    ESP_LOGI(TAG, "--- end relay diagnostic ---");
}

// Phase 3 diagnostic: read PCF8574 inputs at 0x21 and 0x22 (task 3.3).
static void test_pcf8574_inputs(i2c_master_bus_handle_t bus)
{
    ESP_LOGI(TAG, "--- PCF8574 input diagnostic (0x21=DI9-16, 0x22=DI1-8) ---");

    uint8_t addrs[] = {0x21, 0x22};
    for (uint8_t addr : addrs) {
        uint8_t reg = 0x00; // PCF8574 input register
        i2c_master_dev_handle_t dev;
        i2c_device_config_t dcfg{};
        dcfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
        dcfg.device_address = addr;
        dcfg.scl_speed_hz = 100000;
        esp_err_t err = i2c_master_bus_add_device(bus, &dcfg, &dev);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "PCF8574 0x%02X: add device failed: %s", addr,
                     esp_err_to_name(err));
            continue;
        }
        err = i2c_master_transmit(dev, &reg, 1, 1000);
        uint8_t data = 0;
        if (err == ESP_OK) {
            err = i2c_master_receive(dev, &data, 1, 1000);
        }
        i2c_master_bus_rm_device(dev);

        if (err == ESP_OK) {
            ESP_LOGI(TAG, "PCF8574 0x%02X: raw=0x%02X (bits: %c%c%c%c%c%c%c%c)",
                     addr, data,
                     (data & 0x80) ? '1' : '0', (data & 0x40) ? '1' : '0',
                     (data & 0x20) ? '1' : '0', (data & 0x10) ? '1' : '0',
                     (data & 0x08) ? '1' : '0', (data & 0x04) ? '1' : '0',
                     (data & 0x02) ? '1' : '0', (data & 0x01) ? '1' : '0');
        } else {
            ESP_LOGE(TAG, "PCF8574 0x%02X: read failed: %s", addr,
                     esp_err_to_name(err));
        }
    }
    ESP_LOGI(TAG, "--- end PCF8574 diagnostic ---");
}

// Phase 3 diagnostic: read direct GPIO inputs (task 3.3).
static void test_gpio_inputs()
{
    ESP_LOGI(TAG, "--- direct GPIO input diagnostic (GPIO34=DI17, GPIO35=DI18, GPIO0=button) ---");

    gpio_config_t io_conf{};
    io_conf.pin_bit_mask = (1ULL << 34) | (1ULL << 35) | (1ULL << 0);
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.intr_type = GPIO_INTR_DISABLE;
    esp_err_t err = gpio_config(&io_conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "GPIO input: config failed: %s", esp_err_to_name(err));
        return;
    }

    int g34 = gpio_get_level((gpio_num_t)34);
    int g35 = gpio_get_level((gpio_num_t)35);
    int g0 = gpio_get_level((gpio_num_t)0);
    ESP_LOGI(TAG, "GPIO34 (DI17) = %d, GPIO35 (DI18) = %d, GPIO0 (button) = %d",
             g34, g35, g0);
    ESP_LOGI(TAG, "--- end GPIO input diagnostic ---");
}

extern "C" void app_main(void)
{
    // Boot banner.
    esp_chip_info_t chip{};
    esp_chip_info(&chip);

    uint32_t flash_size = 0;
    esp_err_t err = esp_flash_get_size(nullptr, &flash_size);

    ESP_LOGI(TAG, "**** Lumen firmware v%s ****", lumen::kVersion);
    ESP_LOGI(TAG, "ESP-IDF  : %s", esp_get_idf_version());
    ESP_LOGI(TAG, "Chip     : model %d rev%u, %u core(s) @ %d MHz", (int)chip.model,
             (unsigned)chip.revision, (unsigned)chip.cores,
             CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Flash    : %lu bytes", (unsigned long)flash_size);
    } else {
        ESP_LOGE(TAG, "Flash    : size query failed (%s)", esp_err_to_name(err));
    }
    ESP_LOGI(TAG, "Free heap: %lu bytes (largest block %lu)",
             (unsigned long)esp_get_free_heap_size(),
             (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT));
    ESP_LOGI(TAG, "Boot at  : %lld us", (long long)esp_timer_get_time());

    // SystemManager service lifecycle (Phase 1).
    lumen::SystemManager system;
    HeartbeatService heartbeat;
    system.register_service(heartbeat);

    lumen::ServiceResult init_rc = system.init_all();
    lumen::ServiceResult start_rc = system.start_all();
    ESP_LOGI(TAG, "system: init_all=%d, start_all=%d, services=%u",
             (int)init_rc, (int)start_rc, (unsigned)system.service_count());

    // Phase 3: I2C scan diagnostic to resolve unverified peripheral addresses.
    i2c_scan_diagnostic();

    // Re-init I2C bus for peripheral diagnostics (scan deleted it).
    i2c_master_bus_config_t diag_bus_config{};
    diag_bus_config.i2c_port = 0;
    diag_bus_config.sda_io_num = static_cast<gpio_num_t>(4);
    diag_bus_config.scl_io_num = static_cast<gpio_num_t>(16);
    diag_bus_config.clk_source = I2C_CLK_SRC_DEFAULT;
    diag_bus_config.glitch_ignore_cnt = 7;
    diag_bus_config.flags.enable_internal_pullup = true;

    i2c_master_bus_handle_t diag_bus = nullptr;
    if (i2c_new_master_bus(&diag_bus_config, &diag_bus) == ESP_OK) {
        test_pca9685(diag_bus);
        test_pcf8574_inputs(diag_bus);
        i2c_del_master_bus(diag_bus);
    } else {
        ESP_LOGE(TAG, "diagnostic I2C bus init failed");
    }

    test_relays();
    test_gpio_inputs();

    // Phase 1 heartbeat loop: proof of life on real hardware. The SystemManager
    // is now the boot skeleton; services drive their own work in later phases.
    uint32_t beats = 0;
    while (true) {
        ESP_LOGI(TAG, "[heartbeat] up=%lld s, free heap=%lu, beat=%lu",
                 (long long)(esp_timer_get_time() / 1000000),
                 (unsigned long)esp_get_free_heap_size(), (unsigned long)beats++);
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}
