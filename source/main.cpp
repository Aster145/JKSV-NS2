#include "JKSV.hpp"
#include "cmdargs.hpp"
#include "sys/startup.hpp"

#include <switch.h>

int main(int argc, const char *argv[])
{
    sys::boot_log("BEGIN main");
    // Store the pointers here so JKSV can update itself from where ever the user might have placed it.
    sys::boot_log("BEGIN cmdargs::store");
    cmdargs::store(argc, argv);
    sys::boot_log("END cmdargs::store");

    {
        JKSV jksv{};
        sys::boot_log("BEGIN main loop");
        bool firstFrame = true;
        while (jksv.is_running())
        {
            if (firstFrame) { sys::boot_log("BEGIN first update"); }
            jksv.update();
            if (firstFrame)
            {
                sys::boot_log("END first update");
                sys::boot_log("BEGIN first render");
            }
            jksv.render();
            if (firstFrame) { sys::boot_log("END first render"); }
            firstFrame = false;
        }
        sys::boot_log("END main loop: first_frame_completed=%u", static_cast<unsigned>(!firstFrame));
        sys::boot_log("BEGIN main JKSV destruction");
    }
    sys::boot_log("END main JKSV destruction");
    sys::boot_log("END main");
    return 0;
}
