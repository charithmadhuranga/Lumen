// Lumen HAL — Ethernet driver (SPEC §5.1).
//
// PHY clock mode and address are board-profile properties, never hardcoded
// (AGENTS.md §6). Unverified settings are documented in HARDWARE_PROFILES.md.
#pragma once

#include "i_driver.h"

namespace lumen
{

enum class EthPhyAddrSource : uint8_t
{
    kStrap = 0,   // address from PHY strapping
    kFixed = 1,   // address from board profile
};

struct EthConfig
{
    uint8_t phy_addr = 0;
    EthPhyAddrSource phy_addr_source = EthPhyAddrSource::kFixed;
    int mdc_gpio = -1;
    int mdio_gpio = -1;
    int clk_gpio = -1; // -1 = not used / internal
    bool power_pin_used = false;
    int power_gpio = -1;
};

class IEthernetDriver : public IDriver
{
  public:
    virtual esp_err_t configure(const EthConfig &config) = 0;
    virtual esp_err_t start() = 0;
    virtual esp_err_t stop() = 0;

    // Link state: true when link is up.
    virtual esp_err_t get_link_up(bool &link_up) = 0;
};

} // namespace lumen
