// Lumen — PCA9685 driver adapter (src/lighting/drivers/pca9685_adapter.cpp)
//
// Adapts logical RGB/RGBW/CCT channels to PCA9685 16-channel PWM outputs.
// Channel mapping (configurable per channel):
//   dimmer:     channel 0 = brightness
//   rgb:        channels 0,1,2 = R,G,B
//   rgbw:       channels 0,1,2,3 = R,G,B,W
//   cct:        channels 0,1 = warm,cool
//
// Resolves device_id through the hardware manager.
#include "lumen/lighting/driver_adapter.h"

#include <cstring>

#include "lumen/hardware/hal/i_pwm_driver.h"

namespace lumen
{

class Pca9685Adapter final : public IDriverAdapter
{
  public:
    Pca9685Adapter(std::string channel_id, const DriverRef &driver_ref,
                   IHardwareManager &hw_manager)
        : IDriverAdapter(std::move(channel_id)),
          driver_ref_(driver_ref),
          hw_manager_(hw_manager),
          pca9685_(nullptr)
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
        pca9685_ = static_cast<IPwmDriver *>(drv);
        return pca9685_->initialize();
    }

    AdapterResult apply(const DriverDuty &duty) override
    {
        if (pca9685_ == nullptr) {
            return AdapterResult::kDriverNotFound;
        }
        for (uint8_t i = 0; i < duty.channel_count; ++i) {
            esp_err_t err = pca9685_->set_channel_duty(i, duty.channel_duty[i]);
            if (err != ESP_OK) {
                return AdapterResult::kWriteFailed;
            }
        }
        esp_err_t err = pca9685_->flush();
        if (err != ESP_OK) {
            return AdapterResult::kWriteFailed;
        }
        return AdapterResult::kOk;
    }

    esp_err_t safe_state() override
    {
        if (pca9685_ == nullptr) {
            return ESP_ERR_INVALID_STATE;
        }
        for (uint8_t i = 0; i < 16; ++i) {
            pca9685_->set_channel_duty(i, 0);
        }
        return pca9685_->flush();
    }

  private:
    DriverRef driver_ref_;
    IHardwareManager &hw_manager_;
    IPwmDriver *pca9685_;
};

std::unique_ptr<IDriverAdapter> create_pca9685_adapter(
    const std::string &channel_id, const DriverRef &driver_ref,
    IHardwareManager &hw_manager)
{
    return std::make_unique<Pca9685Adapter>(channel_id, driver_ref,
                                            hw_manager);
}

} // namespace lumen
