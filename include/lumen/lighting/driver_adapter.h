// Lumen — driver adapter interface (include/lumen/lighting/driver_adapter.h)
//
// Driver adapters bridge the lighting engine's logical channels to the
// hardware drivers (LEDC, PCA9685, relay, RMT). They resolve device_id
// through the hardware manager — lighting code never touches GPIO directly
// (AGENTS.md §4.1).
//
// Each adapter converts DriverDuty values into the appropriate hardware
// calls for its driver type.
//
// Host-testable: the hardware manager is injected, so tests can use mocks.
#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <memory>

#include "lumen/lighting/channel.h"
#include "lumen/hardware/hal/i_hardware_manager.h"

namespace lumen
{

// Result of applying duties to hardware.
enum class AdapterResult : uint8_t
{
    kOk = 0,
    kChannelNotFound,   // channel not registered
    kDriverNotFound,    // hardware driver not available
    kWriteFailed,       // hardware write failed
};

// Common interface for all driver adapters.
class IDriverAdapter
{
  public:
    virtual ~IDriverAdapter() = default;

    // Initialize the adapter (resolve driver from hardware manager).
    virtual esp_err_t initialize() = 0;

    // Apply duty values to hardware.
    virtual AdapterResult apply(const DriverDuty &duty) = 0;

    // Put outputs into safe state.
    virtual esp_err_t safe_state() = 0;

    // The channel this adapter controls.
    const std::string &channel_id() const { return channel_id_; }

  protected:
    explicit IDriverAdapter(std::string channel_id)
        : channel_id_(std::move(channel_id)) {}

  private:
    std::string channel_id_;
};

// Factory: create the appropriate adapter for a channel's driver type.
// Returns nullptr if the driver type is unsupported.
// Factory: create the adapter for a channel's driver type. `type` decides
// how many consecutive outputs the light uses. Returns nullptr when the
// driver type is unsupported or cannot host this light type.
std::unique_ptr<IDriverAdapter> create_driver_adapter(
    const std::string &channel_id, LightType type, const DriverRef &driver_ref,
    IHardwareManager &hw_manager);

} // namespace lumen
