// Lumen — driver adapters (src/lighting/drivers/driver_adapter.cpp)
//
// Bridge a logical channel's output levels (DriverDuty, 16-bit linear) to
// the HAL driver its DriverRef names. Resolution goes through the hardware
// manager by board-profile id; no GPIO knowledge here (AGENTS.md §4.1).
//
//   pca9685 : outputs [channel .. channel+n-1] (n = outputs of the light
//             type), 16-bit -> 12-bit; writes are BATCHED — the lighting
//             engine flushes every PCA9685 once per tick.
//   ledc    : single native PWM output.
//   relay / gpio : on/off (dimmers on a relay switch at brightness > 0).
//   rmt     : whole-strip colour on an addressable LED driver.
#include "lumen/lighting/driver_adapter.h"

#include "lumen/hardware/hal/i_addressable_led_driver.h"
#include "lumen/hardware/hal/i_pwm_driver.h"
#include "lumen/hardware/hal/i_relay_driver.h"

namespace lumen
{

namespace
{

class PwmMultiAdapter final : public IDriverAdapter
{
  public:
    PwmMultiAdapter(std::string channel_id, const DriverRef &ref, IHardwareManager &hw,
                    uint8_t outputs, bool twelve_bit)
        : IDriverAdapter(std::move(channel_id)), ref_(ref), hw_(hw), outputs_(outputs),
          twelve_bit_(twelve_bit)
    {
    }

    esp_err_t initialize() override
    {
        IDriver *drv = hw_.driver(ref_.device_id);
        if (drv == nullptr) {
            return ESP_ERR_NOT_FOUND;
        }
        if (drv->driver_type() != DriverType::kPwm) {
            return ESP_ERR_INVALID_ARG;
        }
        pwm_ = static_cast<IPwmDriver *>(drv);
        return ESP_OK;
    }

    AdapterResult apply(const DriverDuty &duty) override
    {
        if (pwm_ == nullptr) {
            return AdapterResult::kDriverNotFound;
        }
        for (uint8_t i = 0; i < outputs_; ++i) {
            uint16_t level = duty.channel_duty[i];
            if (twelve_bit_) {
                level = static_cast<uint16_t>((static_cast<uint32_t>(level) * 4095U + 32767U) / 65535U);
            }
            const auto ch = static_cast<uint8_t>(ref_.channel + i);
            if (pwm_->set_channel_duty(ch, level) != ESP_OK) {
                return AdapterResult::kWriteFailed;
            }
        }
        // LEDC writes are immediate; PCA9685 writes are flushed by the engine.
        return AdapterResult::kOk;
    }

    esp_err_t safe_state() override
    {
        if (pwm_ == nullptr) {
            return ESP_ERR_INVALID_STATE;
        }
        for (uint8_t i = 0; i < outputs_; ++i) {
            pwm_->set_channel_duty(static_cast<uint8_t>(ref_.channel + i), 0);
        }
        return ESP_OK;
    }

  private:
    DriverRef ref_;
    IHardwareManager &hw_;
    uint8_t outputs_;
    bool twelve_bit_;
    IPwmDriver *pwm_ = nullptr;
};

class RelayAdapter final : public IDriverAdapter
{
  public:
    RelayAdapter(std::string channel_id, const DriverRef &ref, IHardwareManager &hw)
        : IDriverAdapter(std::move(channel_id)), ref_(ref), hw_(hw)
    {
    }

    esp_err_t initialize() override
    {
        IDriver *drv = hw_.driver(ref_.device_id);
        if (drv == nullptr) {
            return ESP_ERR_NOT_FOUND;
        }
        if (drv->driver_type() != DriverType::kRelay) {
            return ESP_ERR_INVALID_ARG;
        }
        relay_ = static_cast<IRelayDriver *>(drv);
        return ESP_OK;
    }

    AdapterResult apply(const DriverDuty &duty) override
    {
        if (relay_ == nullptr) {
            return AdapterResult::kDriverNotFound;
        }
        const bool on = duty.relay_on && (duty.duty > 0 || duty.channel_duty[0] > 0 ||
                                          duty.channel_count == 0);
        return relay_->set_value(on ? 1 : 0) == ESP_OK ? AdapterResult::kOk
                                                        : AdapterResult::kWriteFailed;
    }

    esp_err_t safe_state() override
    {
        return relay_ != nullptr ? relay_->turn_off() : ESP_ERR_INVALID_STATE;
    }

  private:
    DriverRef ref_;
    IHardwareManager &hw_;
    IRelayDriver *relay_ = nullptr;
};

class AddressableAdapter final : public IDriverAdapter
{
  public:
    AddressableAdapter(std::string channel_id, const DriverRef &ref, IHardwareManager &hw)
        : IDriverAdapter(std::move(channel_id)), ref_(ref), hw_(hw)
    {
    }

    esp_err_t initialize() override
    {
        IDriver *drv = hw_.driver(ref_.device_id);
        if (drv == nullptr) {
            return ESP_ERR_NOT_FOUND;
        }
        if (drv->driver_type() != DriverType::kAddressableLed) {
            return ESP_ERR_INVALID_ARG;
        }
        strip_ = static_cast<IAddressableLedDriver *>(drv);
        return ESP_OK;
    }

    AdapterResult apply(const DriverDuty &duty) override
    {
        if (strip_ == nullptr) {
            return AdapterResult::kDriverNotFound;
        }
        AddressableLedPixel px{};
        px.r = static_cast<uint8_t>(duty.channel_duty[0] >> 8);
        px.g = static_cast<uint8_t>(duty.channel_duty[1] >> 8);
        px.b = static_cast<uint8_t>(duty.channel_duty[2] >> 8);
        px.w = static_cast<uint8_t>(duty.channel_duty[3] >> 8);
        if (strip_->fill(px) != ESP_OK || strip_->show() != ESP_OK) {
            return AdapterResult::kWriteFailed;
        }
        return AdapterResult::kOk;
    }

    esp_err_t safe_state() override
    {
        if (strip_ == nullptr) {
            return ESP_ERR_INVALID_STATE;
        }
        strip_->fill(AddressableLedPixel{});
        return strip_->show();
    }

  private:
    DriverRef ref_;
    IHardwareManager &hw_;
    IAddressableLedDriver *strip_ = nullptr;
};

} // namespace

std::unique_ptr<IDriverAdapter> create_driver_adapter(const std::string &channel_id,
                                                      LightType type,
                                                      const DriverRef &driver_ref,
                                                      IHardwareManager &hw_manager)
{
    const uint8_t outputs = light_type_output_count(type);
    if (driver_ref.type == "pca9685") {
        return std::make_unique<PwmMultiAdapter>(channel_id, driver_ref, hw_manager, outputs, true);
    }
    if (driver_ref.type == "ledc") {
        // A native PWM resource is a single output.
        if (outputs != 1) {
            return nullptr;
        }
        DriverRef single = driver_ref;
        single.channel = 0;
        return std::make_unique<PwmMultiAdapter>(channel_id, single, hw_manager, 1, false);
    }
    if (driver_ref.type == "relay" || driver_ref.type == "gpio") {
        return std::make_unique<RelayAdapter>(channel_id, driver_ref, hw_manager);
    }
    if (driver_ref.type == "rmt") {
        return std::make_unique<AddressableAdapter>(channel_id, driver_ref, hw_manager);
    }
    return nullptr;
}

} // namespace lumen
