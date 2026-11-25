# Lumen application component (SPEC §22 source tree).
#
# Every .c/.cpp under src/ is part of the firmware. Host-only test code
# lives under test/ and is never globbed here. ESP-only sources sit in
# directories the native test env excludes (see platformio.ini).
file(GLOB_RECURSE LUMEN_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

idf_component_register(
    SRCS ${LUMEN_SOURCES}
    INCLUDE_DIRS "." "../include"
    EMBED_TXTFILES
        "../boards/kincony_kc868_ap.json"
        "../boards/generic_esp32.json"
        "../boards/generic_esp32_pca9685.json"
        "../boards/generic_esp32_pcf8574.json"
        "../boards/generic_esp32_pwm.json"
        "../boards/generic_esp32s3.json"
    REQUIRES
        esp_driver_gpio esp_driver_i2c esp_driver_ledc esp_driver_rmt esp_driver_uart
        esp_http_server esp_https_ota esp_http_client esp_wifi esp_eth esp_netif esp_event
        esp_timer esp_partition esp_app_format app_update nvs_flash mbedtls lwip
        spi_flash esp_system log freertos vfs esp_hw_support
        joltwallet__littlefs espressif__mdns espressif__mqtt
)

# AGENTS.md §5: C++17 (applies to C++ sources only; C files are unaffected).
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-missing-field-initializers)
