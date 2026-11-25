cmake_minimum_required(VERSION 3.16)

# Firmware version (app descriptor, checked by OTA): version.txt, or the
# LUMEN_VERSION environment variable for test images (see scripts/pio_version.py).
if(DEFINED ENV{LUMEN_VERSION} AND NOT "$ENV{LUMEN_VERSION}" STREQUAL "")
    set(PROJECT_VER "$ENV{LUMEN_VERSION}")
else()
    file(STRINGS "${CMAKE_CURRENT_LIST_DIR}/version.txt" PROJECT_VER LIMIT_COUNT 1)
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(lumen-firmware)
