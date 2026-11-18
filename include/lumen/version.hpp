// Lumen firmware version — single source of truth for the build version.
// The macro is injected by platformio.ini (build_flags) for every
// environment, so host tests build the same string as the device firmware.
#pragma once

#ifndef LUMEN_FIRMWARE_VERSION
#define LUMEN_FIRMWARE_VERSION "0.0.0"
#endif

namespace lumen
{
inline constexpr const char kVersion[] = LUMEN_FIRMWARE_VERSION;

} // namespace lumen
