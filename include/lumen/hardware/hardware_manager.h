// Lumen — concrete hardware manager.
//
// Builds the live driver set from the active BoardProfile (SPEC §5, §6):
//   i2c resources       -> II2CBus (I2cManager on target)
//   pca9685 peripherals -> Pca9685Driver on its bus (16 outputs "<id>:<n>")
//   pcf8574 peripherals -> Pcf8574Driver on its bus (8 inputs  "<id>:<n>")
//   relay peripherals   -> IRelayDriver on the referenced gpio resource
//   pwm resources       -> IPwmDriver (LEDC on target)
//   gpio output (free)  -> IRelayDriver (plain switched output)
//   gpio input          -> IDigitalInputDriver
//
// Concrete ESP-IDF drivers are created through IDriverFactory so the whole
// manager runs under `pio test -e native` with mock drivers.
//
// Bounded startup: one failing peripheral is recorded (health, errors) and
// never prevents the rest from starting (SPEC §1.2 rule 6).
//
// Thread-safety: callers serialise through lumen::model_mutex() (the
// lighting tick, input polling and the API all take it).
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "lumen/hardware/board_profile.h"
#include "lumen/hardware/drivers/pca9685_driver.h"
#include "lumen/hardware/drivers/pcf8574_driver.h"
#include "lumen/hardware/hal/i_digital_input_driver.h"
#include "lumen/hardware/hal/i_hardware_manager.h"
#include "lumen/hardware/hal/i_i2c_bus.h"
#include "lumen/hardware/hal/i_pwm_driver.h"
#include "lumen/hardware/hal/i_relay_driver.h"

namespace lumen
{

class IDriverFactory
{
  public:
    virtual ~IDriverFactory() = default;
    virtual std::unique_ptr<II2CBus> create_i2c(const BoardResource &res) = 0;
    virtual std::unique_ptr<IRelayDriver> create_relay(const BoardResource &res,
                                                       bool active_low) = 0;
    virtual std::unique_ptr<IPwmDriver> create_pwm(const BoardResource &res) = 0;
    virtual std::unique_ptr<IDigitalInputDriver> create_input(const BoardResource &res) = 0;
};

// Output row for the Hardware UI / status API.
struct HardwareOutputInfo
{
    std::string id;     // "pca9685_0:3", "relay1", "ledc_pwm0"
    std::string name;
    std::string driver; // "pca9685", "relay", "ledc", "gpio"
    std::string device; // owning device id
    int channel = 0;
    bool on = false;
    uint16_t level = 0; // 16-bit
};

struct HardwareInputInfo
{
    std::string id;     // "pcf8574_di1_8:0", "gpio_di17"
    std::string name;   // "DI1"
    std::string source; // "pcf8574" | "gpio"
    bool state = false;
    bool healthy = true;
    uint32_t changed_ms = 0;
};

struct InputEvent
{
    uint16_t index = 0; // index into inputs() (stable while the profile is live)
    bool state = false;
    uint32_t timestamp_ms = 0;
};

struct PeripheralHealth
{
    std::string id;
    std::string type;
    bool ok = false;
    uint32_t errors = 0;
    std::string detail;
};

class HardwareManager final : public IHardwareManager
{
  public:
    static constexpr std::size_t kMaxInputs = 64;

    explicit HardwareManager(IDriverFactory &factory);
    ~HardwareManager() override;

    // Build and initialise every driver the profile declares. Replaces any
    // running driver set (stop() is called first). Returns ESP_OK when the
    // profile was processed; individual failures are in health().
    esp_err_t start(const BoardProfile &profile);

    // Put every output into its safe state and release all drivers.
    void stop();

    bool running() const { return running_; }
    const BoardProfile &profile() const { return profile_; }

    // IHardwareManager
    IDriver *driver(const std::string &id) override;
    esp_err_t flush_outputs() override;

    // First I2C bus, or the bus with the given resource id.
    II2CBus *i2c_bus(const std::string &resource_id = "");

    // Poll every input source (debounced). Returns the number of events.
    std::size_t poll_inputs(uint32_t now_ms, InputEvent *events, std::size_t max_events);

    const std::vector<HardwareInputInfo> &inputs() const { return inputs_; }
    std::vector<HardwareOutputInfo> outputs() const;
    std::vector<PeripheralHealth> health() const;

    // Direct output write for the Hardware screen "test" buttons. `level` is
    // 16-bit; relays treat any non-zero level as on.
    esp_err_t set_output(const std::string &output_id, uint16_t level);

    // Read an output's level back from the hardware (PCA9685 registers;
    // other drivers report their latched value). 16-bit level.
    esp_err_t read_output(const std::string &output_id, uint16_t &level);

    // Names of drivers that failed to initialise (for diagnostics/logs).
    std::size_t failed_count() const;

  private:
    struct DriverEntry
    {
        std::string id;
        std::string type; // "i2c", "pca9685", "relay", "ledc", "gpio"
        std::unique_ptr<IDriver> owned;
        IDriver *driver = nullptr; // == owned.get(), nullptr when init failed
        esp_err_t init_err = ESP_OK;
        int channels = 1;
    };
    struct PcfEntry
    {
        std::string id;
        std::unique_ptr<Pcf8574Driver> driver;
        esp_err_t init_err = ESP_OK;
        std::size_t first_input = 0; // index into inputs_
        int channels = 8;
    };
    struct GpioInputEntry
    {
        std::unique_ptr<IDigitalInputDriver> driver;
        esp_err_t init_err = ESP_OK;
        std::size_t input_index = 0;
        bool stable = false;
        bool pending = false;
        uint32_t since_ms = 0;
    };

    DriverEntry *find_entry(const std::string &id);
    const DriverEntry *find_entry(const std::string &id) const;

    IDriverFactory &factory_;
    BoardProfile profile_;
    bool running_ = false;
    std::vector<DriverEntry> drivers_;
    std::vector<PcfEntry> pcfs_;
    std::vector<GpioInputEntry> gpio_inputs_;
    std::vector<HardwareInputInfo> inputs_;
};

} // namespace lumen
