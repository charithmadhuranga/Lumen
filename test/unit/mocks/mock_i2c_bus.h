// Lumen — mock II2CBus for host tests (header-only).
//
// Simulates an I2C bus with programmable per-address device responses,
// transaction counting (for batching tests), and error injection.
#pragma once

#include "lumen/hardware/hal/i_i2c_bus.h"

#include <cstdint>
#include <map>
#include <vector>

namespace lumen
{

class MockI2cBus final : public II2CBus
{
  public:
    struct Transaction
    {
        uint8_t device_addr = 0;
        bool write = true;
        std::vector<uint8_t> data;
    };

    MockI2cBus() : state_(DriverState::kUninitialized) {}

    // IDriver
    esp_err_t initialize() override
    {
        state_ = DriverState::kInitialized;
        return ESP_OK;
    }
    esp_err_t shutdown() override
    {
        state_ = DriverState::kUninitialized;
        return ESP_OK;
    }
    DriverState state() const override { return state_; }
    const char *name() const override { return "mock_i2c"; }
    DriverType driver_type() const override { return DriverType::kI2c; }

    // II2CBus
    esp_err_t write(uint8_t device_addr, const uint8_t *write_buf,
                    size_t write_len) override
    {
        Transaction t;
        t.device_addr = device_addr;
        t.write = true;
        t.data.assign(write_buf, write_buf + write_len);
        transactions_.push_back(t);

        auto it = device_errors_.find(device_addr);
        if (it != device_errors_.end()) {
            last_error_ = it->second;
            return it->second;
        }
        last_error_ = ESP_OK;
        return ESP_OK;
    }

    esp_err_t read(uint8_t device_addr, uint8_t *read_buf,
                   size_t read_len) override
    {
        Transaction t;
        t.device_addr = device_addr;
        t.write = false;
        transactions_.push_back(t);

        auto it = device_errors_.find(device_addr);
        if (it != device_errors_.end()) {
            last_error_ = it->second;
            return it->second;
        }

        auto dit = devices_.find(device_addr);
        if (dit == devices_.end()) {
            last_error_ = ESP_ERR_NOT_FOUND;
            return ESP_ERR_NOT_FOUND;
        }
        const auto &bytes = dit->second;
        for (size_t i = 0; i < read_len && i < bytes.size(); ++i) {
            read_buf[i] = bytes[i];
        }
        last_error_ = ESP_OK;
        return ESP_OK;
    }

    esp_err_t write_then_read(uint8_t device_addr,
                              const uint8_t *write_buf, size_t write_len,
                              uint8_t *read_buf, size_t read_len) override
    {
        Transaction t;
        t.device_addr = device_addr;
        t.write = true;
        t.data.assign(write_buf, write_buf + write_len);
        transactions_.push_back(t);

        auto it = device_errors_.find(device_addr);
        if (it != device_errors_.end()) {
            last_error_ = it->second;
            return it->second;
        }
        last_error_ = ESP_OK;
        return read(device_addr, read_buf, read_len);
    }

    esp_err_t probe(uint8_t device_addr) override
    {
        auto dit = devices_.find(device_addr);
        if (dit != devices_.end()) {
            return ESP_OK;
        }
        return ESP_ERR_NOT_FOUND;
    }

    esp_err_t scan(uint8_t *found, size_t max_found,
                   size_t &found_count) override
    {
        found_count = 0;
        for (const auto &kv : devices_) {
            if (found_count >= max_found) {
                break;
            }
            found[found_count++] = kv.first;
        }
        return ESP_OK;
    }

    esp_err_t bus_reset() override
    {
        last_error_ = ESP_OK;
        return ESP_OK;
    }

    // --- test control ---
    void add_device(uint8_t addr, std::vector<uint8_t> read_bytes)
    {
        devices_[addr] = std::move(read_bytes);
        device_errors_.erase(addr);
    }

    void set_device_byte(uint8_t addr, uint8_t byte)
    {
        devices_[addr] = {byte};
    }

    void inject_error(uint8_t addr, esp_err_t err)
    {
        device_errors_[addr] = err;
    }

    void clear_transactions() { transactions_.clear(); }

    const std::vector<Transaction> &transactions() const
    {
        return transactions_;
    }

    std::size_t transaction_count() const { return transactions_.size(); }

    esp_err_t last_error() const { return last_error_; }

  private:
    std::map<uint8_t, std::vector<uint8_t>> devices_;
    std::map<uint8_t, esp_err_t> device_errors_;
    std::vector<Transaction> transactions_;
    esp_err_t last_error_ = ESP_OK;
    DriverState state_;
};

} // namespace lumen
