#pragma once
#include "build_options.hpp"
#include "appstates/MainMenuState.hpp"
#include "sys/sys.hpp"

namespace tasks::mainmenu
{
    void backup_all_for_all_local(sys::threadpool::JobData taskData);
#if JKSV_ENABLE_REMOTE
    void backup_all_for_all_remote(sys::threadpool::JobData taskData);
#endif
}
