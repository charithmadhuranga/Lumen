// Lumen — relay driver adapter (src/lighting/drivers/relay_adapter.cpp)
//
// Adapts a logical switch channel to a relay driver.
// Resolves device_id through the hardware manager.
#include "lumen/lighting/driver_adapter.h"

#include <cstring>

#include "lumen/hardware/hal/i_relay_driver.h"

namespace lumen
{

class RelayAdapter final : public IDriverAdapter
{
  public:
    RelayAdapter(std::string channel_id, const DriverRef &driver_ref,
                 IHardwareManager &hw_manager)
        : IDriverAdapter(std::move(channel_id)),
          driver_ref_(driver_ref),
          hw_manager_(hw_manager),
          relay_(nullptr)
    {
    }

    esp_err_t initialize() override
    {
        IDriver *drv = hw_manager_.driver(driver_ref_.device_id);
        if (drv == nullptr) {
            return ESP_ERR_NOT_FOUND;
        }
        if (drv->driver_type() != DriverType::kRelay) {
            return ESP_ERR_INVALID_ARG;
        }
        relay_ = static_cast<IRelayDriver *>(drv);
        return relay_->initialize();
    }

    AdapterResult apply(const DriverDuty &duty) override
    {
        if (relay_ == nullptr) {
            return AdapterResult::kDriverNotFound;
        }
        esp_err_t err = duty.relay_on ? relay_->turn_on() : relay_->turn_off();
        if (err != ESP_OK) {
            return AdapterResult::kWriteFailed;
        }
        return AdapterResult::kOk;
    }

    esp_err_t safe_state() override
    {
        if (relay_ == nullptr) {
            return ESP_ERR_INVALID_STATE;
        }
        return relay_->turn_off();
    }

  private:
    DriverRef driver_ref_;
    IHardwareManager &hw_manager_;
    IRelayDriver *relay_;
};

std::unique_ptr<IDriverAdapter> create_relay_adapter(
    const std::string &channel_id, const DriverRef &driver_ref,
    IHardwareManager &hw_manager)
{
    return std::make_unique<RelayAdapter>(channel_id, driver_ref,
                                          hw_manager);
}

} // namespace lumen
