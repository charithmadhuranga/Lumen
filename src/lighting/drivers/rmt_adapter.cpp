// Lumen — RMT driver adapter (src/lighting/drivers/rmt_adapter.cpp)
//
// Adapts a logical addressable_rgb/addressable_rgbw channel to an RMT
// addressable LED driver.
// Resolves device_id through the hardware manager.
#include "lumen/lighting/driver_adapter.h"

#include <cstring>

#include "lumen/hardware/hal/i_addressable_led_driver.h"

namespace lumen
{

class RmtAdapter final : public IDriverAdapter
{
  public:
    RmtAdapter(std::string channel_id, const DriverRef &driver_ref,
               IHardwareManager &hw_manager)
        : IDriverAdapter(std::move(channel_id)),
          driver_ref_(driver_ref),
          hw_manager_(hw_manager),
          led_driver_(nullptr)
    {
    }

    esp_err_t initialize() override
    {
        IDriver *drv = hw_manager_.driver(driver_ref_.device_id);
        if (drv == nullptr) {
            return ESP_ERR_NOT_FOUND;
        }
        if (drv->driver_type() != DriverType::kAddressableLed) {
            return ESP_ERR_INVALID_ARG;
        }
        led_driver_ = static_cast<IAddressableLedDriver *>(drv);
        return led_driver_->initialize();
    }

    AdapterResult apply(const DriverDuty &duty) override
    {
        if (led_driver_ == nullptr) {
            return AdapterResult::kDriverNotFound;
        }
        // channel_duty[] holds per-LED RGB packed as consecutive R,G,B values.
        for (uint8_t i = 0; i + 2 < duty.channel_count; i += 3) {
            AddressableLedPixel pixel{};
            pixel.r = duty.channel_duty[i] & 0xFF;
            pixel.g = duty.channel_duty[i + 1] & 0xFF;
            pixel.b = duty.channel_duty[i + 2] & 0xFF;
            uint8_t led_index = i / 3;
            esp_err_t err = led_driver_->set_pixel(led_index, pixel);
            if (err != ESP_OK) {
                return AdapterResult::kWriteFailed;
            }
        }
        esp_err_t err = led_driver_->show();
        if (err != ESP_OK) {
            return AdapterResult::kWriteFailed;
        }
        return AdapterResult::kOk;
    }

    esp_err_t safe_state() override
    {
        if (led_driver_ == nullptr) {
            return ESP_ERR_INVALID_STATE;
        }
        AddressableLedPixel off{0, 0, 0, 0};
        for (uint16_t i = 0; i < 60; ++i) {
            led_driver_->set_pixel(i, off);
        }
        return led_driver_->show();
    }

  private:
    DriverRef driver_ref_;
    IHardwareManager &hw_manager_;
    IAddressableLedDriver *led_driver_;
};

std::unique_ptr<IDriverAdapter> create_rmt_adapter(
    const std::string &channel_id, const DriverRef &driver_ref,
    IHardwareManager &hw_manager)
{
    return std::make_unique<RmtAdapter>(channel_id, driver_ref,
                                        hw_manager);
}

} // namespace lumen
