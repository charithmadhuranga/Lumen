# Lumen application component (SPEC §22 source tree).
# Components under src/ (core, hardware, lighting, automation, networking,
# web, services) register themselves as phases land; each component
# registers only what it uses.

idf_component_register(
    SRCS
        "main.cpp"
        "core/event_bus.cpp"
        "core/storage_manager.cpp"
        "core/json_adapter.cpp"
        "core/config_manager.cpp"
        "core/system_manager.cpp"
        "core/diagnostics.cpp"
        "core/cJSON.c"
        "hardware/hal/pin_validator.cpp"
        "hardware/hal/board_profile_loader.cpp"
        "hardware/drivers/gpio_driver.cpp"
        "hardware/drivers/relay_driver.cpp"
        "hardware/drivers/ledc_driver.cpp"
        "hardware/drivers/i2c_manager.cpp"
        "hardware/drivers/pca9685_driver.cpp"
        "hardware/drivers/pcf8574_driver.cpp"
        "lighting/channel_manager.cpp"
        "lighting/lighting_task.cpp"
        "lighting/effects_engine.cpp"
        "lighting/drivers/driver_adapter.cpp"
        "lighting/drivers/ledc_adapter.cpp"
        "lighting/drivers/pca9685_adapter.cpp"
        "lighting/drivers/relay_adapter.cpp"
        "lighting/drivers/rmt_adapter.cpp"
        "lighting/room_manager.cpp"
        "lighting/group_manager.cpp"
        "lighting/scene_manager.cpp"
        "web/web_server.cpp"
        "web/auth_manager.cpp"
        "web/websocket_manager.cpp"
        "web/board_json.cpp"
        "web/api_routes.cpp"
    INCLUDE_DIRS "." "../include"
)

# AGENTS.md §5: C++17 (applies to C++ sources only; C files are unaffected).
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
