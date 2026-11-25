// Lumen — ESP-IDF driver factory (target only; excluded from native tests).
//
// Creates the concrete ESP-IDF drivers the HardwareManager asks for. All pin
// numbers come from the board-profile resource passed in (AGENTS.md §4.1).
#include "lumen/hardware/platform/esp_driver_factory.h"

#include "lumen/hardware/drivers/gpio_driver.h"
#include "lumen/hardware/drivers/i2c_manager.h"
#include "lumen/hardware/drivers/ledc_driver.h"
#include "lumen/hardware/drivers/relay_driver.h"

namespace lumen
{

std::unique_ptr<II2CBus> EspDriverFactory::create_i2c(const BoardResource &res)
{
    I2cBusConfig cfg;
    cfg.port = res.port >= 0 ? res.port : 0;
    cfg.sda_gpio = res.pin("sda");
    cfg.scl_gpio = res.pin("scl");
    cfg.frequency_hz = res.frequency > 0 ? static_cast<uint32_t>(res.frequency) : 100000U;
    return std::make_unique<I2cManager>(cfg);
}

std::unique_ptr<IRelayDriver> EspDriverFactory::create_relay(const BoardResource &res,
                                                             bool active_low)
{
    auto relay = std::make_unique<RelayDriver>(
        res.gpio, active_low ? RelayPolarity::kActiveLow : RelayPolarity::kActiveHigh);
    relay->set_safe_state(res.safe_state_set && res.safe_state ? 1 : 0, true);
    return relay;
}

std::unique_ptr<IPwmDriver> EspDriverFactory::create_pwm(const BoardResource &res)
{
    const uint32_t freq = res.frequency > 0 ? static_cast<uint32_t>(res.frequency) : 1000U;
    return std::make_unique<LedcDriver>(res.gpio, freq, 13, res.inverted);
}

std::unique_ptr<IDigitalInputDriver> EspDriverFactory::create_input(const BoardResource &res)
{
    auto in = std::make_unique<GpioDriver>(res.gpio, "input", res.inverted);
    in->set_pull(res.pull.empty() ? "none" : res.pull);
    return in;
}

} // namespace lumen
