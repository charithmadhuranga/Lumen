// Lumen — LEDC driver adapter (src/lighting/drivers/ledc_adapter.cpp)
//
// Adapts a logical dimmer/switch channel to an LEDC PWM channel.
// Resolves device_id through the hardware manager.
// Pure C++ core; the actual LEDC driver (IPwmDriver) is obtained from
// IHardwareManager at init time.
#include "lumen/lighting/driver_adapter.h"

#include <cstring>

#include "lumen/hardware/hal/i_pwm_driver.h"

namespace lumen
{

class LedcAdapter final : public IDriverAdapter
{
  public:
    LedcAdapter(std::string channel_id, const DriverRef &driver_ref,
                IHardwareManager &hw_manager)
        : IDriverAdapter(std::move(channel_id)),
          driver_ref_(driver_ref),
          hw_manager_(hw_manager),
          pwm_driver_(nullptr)
    {
    }

    esp_err_t initialize() override
    {
        IDriver *drv = hw_manager_.driver(driver_ref_.device_id);
        if (drv == nullptr) {
            return ESP_ERR_NOT_FOUND;
        }
        if (drv->driver_type() != DriverType::kPwm) {
            return ESP_ERR_INVALID_ARG;
        }
        pwm_driver_ = static_cast<IPwmDriver *>(drv);
        return pwm_driver_->initialize();
    }

    AdapterResult apply(const DriverDuty &duty) override
    {
        if (pwm_driver_ == nullptr) {
            return AdapterResult::kDriverNotFound;
        }
        esp_err_t err = pwm_driver_->set_duty(duty.duty);
        if (err != ESP_OK) {
            return AdapterResult::kWriteFailed;
        }
        return AdapterResult::kOk;
    }

    esp_err_t safe_state() override
    {
        if (pwm_driver_ == nullptr) {
            return ESP_ERR_INVALID_STATE;
        }
        return pwm_driver_->set_duty(0);
    }

  private:
    DriverRef driver_ref_;
    IHardwareManager &hw_manager_;
    IPwmDriver *pwm_driver_;
};

std::unique_ptr<IDriverAdapter> create_ledc_adapter(
    const std::string &channel_id, const DriverRef &driver_ref,
    IHardwareManager &hw_manager)
{
    return std::make_unique<LedcAdapter>(channel_id, driver_ref, hw_manager);
}

} // namespace lumen
