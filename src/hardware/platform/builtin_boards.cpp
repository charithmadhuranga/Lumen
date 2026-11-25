// Lumen — built-in board profiles embedded into the firmware image
// (CMake EMBED_TXTFILES, see src/CMakeLists.txt). Read-only by design:
// users duplicate a built-in profile to customise it (SPEC §6.1).
#include "lumen/hardware/board_service.h"

#define LUMEN_EMBEDDED_BOARD(sym)                                                  \
    extern const char _binary_##sym##_json_start[] asm("_binary_" #sym "_json_start")

LUMEN_EMBEDDED_BOARD(kincony_kc868_ap);
LUMEN_EMBEDDED_BOARD(generic_esp32);
LUMEN_EMBEDDED_BOARD(generic_esp32_pca9685);
LUMEN_EMBEDDED_BOARD(generic_esp32_pcf8574);
LUMEN_EMBEDDED_BOARD(generic_esp32_pwm);
LUMEN_EMBEDDED_BOARD(generic_esp32s3);

namespace lumen
{

const std::vector<BuiltinBoard> &builtin_boards()
{
    static const std::vector<BuiltinBoard> boards = {
        {"kincony_kc868_ap", _binary_kincony_kc868_ap_json_start},
        {"generic_esp32", _binary_generic_esp32_json_start},
        {"generic_esp32_pca9685", _binary_generic_esp32_pca9685_json_start},
        {"generic_esp32_pcf8574", _binary_generic_esp32_pcf8574_json_start},
        {"generic_esp32_pwm", _binary_generic_esp32_pwm_json_start},
        {"generic_esp32s3", _binary_generic_esp32s3_json_start},
    };
    return boards;
}

} // namespace lumen
