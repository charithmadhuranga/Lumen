// Lumen — native stub for esp_err.h (host unit tests only).
//
// ESP-IDF's esp_err_t and error codes, stubbed so the pure-C++ driver
// headers (pca9685, pcf8574) compile on the native test target. The real
// ESP-IDF header is used on-device; this stub is only reachable from native
// builds via -I test/unit/mocks.
#pragma once

#include <cstdint>

typedef uint32_t esp_err_t;

#define ESP_OK ((esp_err_t)0)
#define ESP_FAIL ((esp_err_t)1)
#define ESP_ERR_NO_MEM ((esp_err_t)0x101)
#define ESP_ERR_INVALID_ARG ((esp_err_t)0x104)
#define ESP_ERR_INVALID_STATE ((esp_err_t)0x105)
#define ESP_ERR_NOT_FOUND ((esp_err_t)0x101)
#define ESP_ERR_NOT_SUPPORTED ((esp_err_t)0x102)
#define ESP_ERR_TIMEOUT ((esp_err_t)0x103)
