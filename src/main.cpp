// Lumen — universal ESP32 smart lighting controller.
//
// Entry point: the Controller owns every subsystem (see
// include/lumen/app/controller.hpp and docs/ARCHITECTURE.md for the boot
// sequence and task registry). app_main returns once boot is complete; the
// lighting/housekeeping/httpd tasks keep running.
#include "lumen/app/controller.hpp"

extern "C" void app_main(void)
{
    static lumen::Controller controller; // lives for the whole uptime
    controller.start();
}
