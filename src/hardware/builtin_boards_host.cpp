// Lumen — built-in board profiles for host builds (native tests): read from
// the repository's boards/ directory. The target build embeds the same
// files into flash (src/hardware/platform/builtin_boards.cpp).
#ifndef ESP_PLATFORM
#include <fstream>
#include <sstream>
#include <string>

#include "lumen/hardware/board_service.h"

#ifndef LUMEN_REPO_ROOT
#define LUMEN_REPO_ROOT "."
#endif

namespace lumen
{

const std::vector<BuiltinBoard> &builtin_boards()
{
    static const char *ids[] = {"kincony_kc868_ap",     "generic_esp32",
                                "generic_esp32_pca9685", "generic_esp32_pcf8574",
                                "generic_esp32_pwm",     "generic_esp32s3"};
    static std::vector<std::string> storage;
    static std::vector<BuiltinBoard> boards;
    if (boards.empty()) {
        storage.reserve(sizeof(ids) / sizeof(ids[0]));
        for (const char *id : ids) {
            std::ifstream f(std::string(LUMEN_REPO_ROOT) + "/boards/" + id + ".json");
            std::stringstream ss;
            ss << f.rdbuf();
            storage.push_back(ss.str());
            boards.push_back({id, storage.back().c_str()});
        }
    }
    return boards;
}

} // namespace lumen
#endif
