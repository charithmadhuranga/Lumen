// Lumen — I2C manager implementation (ESP-IDF). See i2c_manager.h
#include "driver/gpio.h"
#include "driver/i2c_master.h"

#include "lumen/hardware/drivers/i2c_manager.h"

namespace lumen
{

struct I2cDeviceHandle
{
    uint8_t address;
    i2c_master_dev_handle_t handle;
};

I2cManager::I2cManager()
    : port_(0), sda_gpio_(-1), scl_gpio_(-1), frequency_hz_(100000)
{
}

esp_err_t I2cManager::initialize()
{
    if (state_ != DriverState::kUninitialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (sda_gpio_ < 0 || scl_gpio_ < 0) {
        return ESP_ERR_INVALID_ARG;
    }

    i2c_master_bus_config_t bus_config{};
    bus_config.i2c_port = static_cast<i2c_port_t>(port_);
    bus_config.sda_io_num = static_cast<gpio_num_t>(sda_gpio_);
    bus_config.scl_io_num = static_cast<gpio_num_t>(scl_gpio_);
    bus_config.clk_source = I2C_CLK_SRC_DEFAULT;
    bus_config.glitch_ignore_cnt = 7;
    bus_config.flags.enable_internal_pullup = true;

    esp_err_t err =
        i2c_new_master_bus(&bus_config, reinterpret_cast<i2c_master_bus_handle_t *>(&bus_handle_));
    if (err != ESP_OK) {
        bus_handle_ = nullptr;
        state_ = DriverState::kFailed;
        return err;
    }

    state_ = DriverState::kInitialized;
    return ESP_OK;
}

esp_err_t I2cManager::shutdown()
{
    // Remove all devices first handled by callers via remove_device.
    esp_err_t err = ESP_OK;
    if (bus_handle_) {
        err = i2c_del_master_bus(reinterpret_cast<i2c_master_bus_handle_t>(bus_handle_));
        bus_handle_ = nullptr;
    }
    state_ = DriverState::kUninitialized;
    return err;
}

DriverState I2cManager::state() const
{
    return state_;
}

const char *I2cManager::name() const
{
    return "i2c";
}

esp_err_t I2cManager::write(uint8_t device_addr, const uint8_t *write_buf,
                            size_t write_len)
{
    if (state_ != DriverState::kInitialized || !bus_handle_ || !write_buf) {
        return ESP_ERR_INVALID_STATE;
    }
    // Find device handle. For simplicity, probe-based: use transient device.
    // In production, callers should use add_device for repeated access.
    i2c_master_dev_handle_t dev;
    i2c_device_config_t dev_config{};
    dev_config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dev_config.device_address = device_addr;
    dev_config.scl_speed_hz = frequency_hz_;
    esp_err_t err = i2c_master_bus_add_device(
        reinterpret_cast<i2c_master_bus_handle_t>(bus_handle_), &dev_config, &dev);
    if (err != ESP_OK) {
        return err;
    }
    err = i2c_master_transmit(dev, write_buf, write_len, 1000);
    i2c_master_bus_rm_device(dev);
    return err;
}

esp_err_t I2cManager::read(uint8_t device_addr, uint8_t *read_buf,
                           size_t read_len)
{
    if (state_ != DriverState::kInitialized || !bus_handle_ || !read_buf) {
        return ESP_ERR_INVALID_STATE;
    }
    i2c_master_dev_handle_t dev;
    i2c_device_config_t dev_config{};
    dev_config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dev_config.device_address = device_addr;
    dev_config.scl_speed_hz = frequency_hz_;
    esp_err_t err = i2c_master_bus_add_device(
        reinterpret_cast<i2c_master_bus_handle_t>(bus_handle_), &dev_config, &dev);
    if (err != ESP_OK) {
        return err;
    }
    err = i2c_master_receive(dev, read_buf, read_len, 1000);
    i2c_master_bus_rm_device(dev);
    return err;
}

esp_err_t I2cManager::write_then_read(uint8_t device_addr,
                                      const uint8_t *write_buf,
                                      size_t write_len, uint8_t *read_buf,
                                      size_t read_len)
{
    if (state_ != DriverState::kInitialized || !bus_handle_) {
        return ESP_ERR_INVALID_STATE;
    }
    i2c_master_dev_handle_t dev;
    i2c_device_config_t dev_config{};
    dev_config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dev_config.device_address = device_addr;
    dev_config.scl_speed_hz = frequency_hz_;
    esp_err_t err = i2c_master_bus_add_device(
        reinterpret_cast<i2c_master_bus_handle_t>(bus_handle_), &dev_config, &dev);
    if (err != ESP_OK) {
        return err;
    }
    if (write_buf && write_len > 0) {
        err = i2c_master_transmit(dev, write_buf, write_len, 1000);
    }
    if (err == ESP_OK && read_buf && read_len > 0) {
        err = i2c_master_receive(dev, read_buf, read_len, 1000);
    }
    i2c_master_bus_rm_device(dev);
    return err;
}

esp_err_t I2cManager::probe(uint8_t device_addr)
{
    if (state_ != DriverState::kInitialized || !bus_handle_) {
        return ESP_ERR_INVALID_STATE;
    }
    return i2c_master_probe(reinterpret_cast<i2c_master_bus_handle_t>(bus_handle_),
                            device_addr, 1000);
}

esp_err_t I2cManager::scan(uint8_t *found, size_t max_found,
                           size_t &found_count)
{
    found_count = 0;
    if (state_ != DriverState::kInitialized || !bus_handle_ || !found ||
        max_found == 0) {
        return ESP_ERR_INVALID_STATE;
    }
    // Address 0 is reserved (general call), skip it. Scan 0x08..0x7F.
    for (uint8_t addr = 0x08; addr <= 0x77 && found_count < max_found;
         ++addr) {
        if (i2c_master_probe(reinterpret_cast<i2c_master_bus_handle_t>(bus_handle_),
                             addr, 500) == ESP_OK) {
            found[found_count++] = addr;
        }
    }
    return ESP_OK;
}

esp_err_t I2cManager::bus_reset()
{
    if (state_ != DriverState::kInitialized || !bus_handle_) {
        return ESP_ERR_INVALID_STATE;
    }
    // Bit-bang SCL to recover from a stuck bus: clock out up to 9 cycles.
    gpio_set_direction(static_cast<gpio_num_t>(scl_gpio_), GPIO_MODE_OUTPUT);
    gpio_set_level(static_cast<gpio_num_t>(scl_gpio_), 1);
    for (int i = 0; i < 9; ++i) {
        gpio_set_level(static_cast<gpio_num_t>(scl_gpio_), 0);
        gpio_set_level(static_cast<gpio_num_t>(scl_gpio_), 1);
    }
    // Re-init SDA/SCL as inputs.
    gpio_set_direction(static_cast<gpio_num_t>(sda_gpio_), GPIO_MODE_INPUT);
    gpio_set_direction(static_cast<gpio_num_t>(scl_gpio_), GPIO_MODE_INPUT);
    return ESP_OK;
}

esp_err_t I2cManager::add_device(uint8_t device_addr,
                                 I2cDeviceHandle **handle)
{
    (void)device_addr;
    (void)handle;
    return ESP_ERR_NOT_SUPPORTED; // transient devices used for now
}

esp_err_t I2cManager::remove_device(I2cDeviceHandle *handle)
{
    (void)handle;
    return ESP_ERR_NOT_SUPPORTED;
}

} // namespace lumen
