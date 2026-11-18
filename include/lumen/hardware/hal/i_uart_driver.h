// Lumen HAL — UART driver (SPEC §5.1).
//
// Used for RS485 and generic serial. Baud, data/stop/parity, and flow
// control are configured per-instance.
#pragma once

#include "i_driver.h"

#include <cstdint>

namespace lumen
{

enum class UartParity : uint8_t
{
    kNone = 0,
    kEven,
    kOdd,
};

enum class UartFlowControl : uint8_t
{
    kNone = 0,
    kRts,
    kCts,
};

// A UART instance with configurable baud/format/flow control.
class IUartDriver : public IDriver
{
  public:
    // Configure baud rate, data bits, stop bits, parity, flow control.
    virtual esp_err_t configure(uint32_t baud_rate, uint8_t data_bits,
                                uint8_t stop_bits, UartParity parity,
                                UartFlowControl flow_control) = 0;

    // Write up to `tx_len` bytes; `tx_written` reports how many were sent.
    virtual esp_err_t write(const uint8_t *tx_buf, size_t tx_len,
                            size_t &tx_written) = 0;

    // Read up to `rx_len` bytes into `rx_buf`; `rx_read` reports how many.
    // Returns ESP_ERR_TIMEOUT when no data within the configured timeout.
    virtual esp_err_t read(uint8_t *rx_buf, size_t rx_len,
                           size_t &rx_read) = 0;

    virtual esp_err_t set_baud_rate(uint32_t baud_rate) = 0;
    virtual esp_err_t get_baud_rate(uint32_t &baud_rate) = 0;
};

} // namespace lumen
