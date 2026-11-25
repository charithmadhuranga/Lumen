// Lumen — IAutomationHost implementation over the app context (target).
#pragma once

#include "lumen/app/app_context.hpp"
#include "lumen/automation/automation_engine.h"

namespace lumen
{

class AppAutomationHost final : public IAutomationHost
{
  public:
    explicit AppAutomationHost(AppContext &ctx) : ctx_(ctx) {}

    bool light_state(const std::string &id, LightState &out) override;
    bool input_state(const std::string &id, bool &out) override;
    bool exists(const char *kind, const std::string &id) override;
    bool light_command(const LightCommand &cmd) override;
    bool scene_activate(const std::string &id, const std::string &origin) override;
    bool group_command(const std::string &id, bool is_room, const LightCommand &cmd) override;
    bool output_toggle(const std::string &output_id) override;
    bool mqtt_publish(const std::string &topic, const std::string &payload, bool retain) override;
    bool udp_send(const std::string &host, uint16_t port, const std::string &payload) override;
    bool tcp_send(const std::string &host, uint16_t port, const std::string &payload) override;

    // Wall clock + sunrise/sunset (recomputed once per local day).
    AutoClock clock();
    void set_location(double lat, double lon);
    bool location(double &lat, double &lon) const;

  private:
    AppContext &ctx_;
    bool has_location_ = false;
    double latitude_ = 0;
    double longitude_ = 0;
    int sun_day_ = -1;
    int sunrise_ = -1;
    int sunset_ = -1;
};

} // namespace lumen
