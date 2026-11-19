// Lumen — driver adapter factory (src/lighting/drivers/driver_adapter.cpp)
//
// Factory function that dispatches to the appropriate adapter based on
// driver type. Individual adapters register their create functions here.
#include "lumen/lighting/driver_adapter.h"

#include <memory>
#include <string>

namespace lumen
{

// Forward declarations of per-adapter creation functions.
std::unique_ptr<IDriverAdapter> create_ledc_adapter(
    const std::string &channel_id, const DriverRef &driver_ref,
    IHardwareManager &hw_manager);
std::unique_ptr<IDriverAdapter> create_pca9685_adapter(
    const std::string &channel_id, const DriverRef &driver_ref,
    IHardwareManager &hw_manager);
std::unique_ptr<IDriverAdapter> create_relay_adapter(
    const std::string &channel_id, const DriverRef &driver_ref,
    IHardwareManager &hw_manager);
std::unique_ptr<IDriverAdapter> create_rmt_adapter(
    const std::string &channel_id, const DriverRef &driver_ref,
    IHardwareManager &hw_manager);

std::unique_ptr<IDriverAdapter> create_driver_adapter(
    const std::string &channel_id, const DriverRef &driver_ref,
    IHardwareManager &hw_manager)
{
    if (driver_ref.type == "ledc") {
        return create_ledc_adapter(channel_id, driver_ref, hw_manager);
    }
    if (driver_ref.type == "pca9685") {
        return create_pca9685_adapter(channel_id, driver_ref, hw_manager);
    }
    if (driver_ref.type == "relay") {
        return create_relay_adapter(channel_id, driver_ref, hw_manager);
    }
    if (driver_ref.type == "rmt") {
        return create_rmt_adapter(channel_id, driver_ref, hw_manager);
    }
    return nullptr;
}

} // namespace lumen
