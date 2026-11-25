// Lumen — ESP-IDF driver factory (target only).
#pragma once

#include "lumen/hardware/hardware_manager.h"

namespace lumen
{

class EspDriverFactory final : public IDriverFactory
{
  public:
    std::unique_ptr<II2CBus> create_i2c(const BoardResource &res) override;
    std::unique_ptr<IRelayDriver> create_relay(const BoardResource &res,
                                               bool active_low) override;
    std::unique_ptr<IPwmDriver> create_pwm(const BoardResource &res) override;
    std::unique_ptr<IDigitalInputDriver> create_input(const BoardResource &res) override;
};

} // namespace lumen
