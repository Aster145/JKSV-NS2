#pragma once
#include "build_options.hpp"
#include "appstates/UserOptionState.hpp"
#include "sys/sys.hpp"

namespace tasks::useroptions
{
    void backup_all_for_user_local(sys::threadpool::JobData taskData);
#if JKSV_ENABLE_REMOTE
    void backup_all_for_user_remote(sys::threadpool::JobData taskData);
#endif
    void create_all_save_data_for_user(sys::threadpool::JobData taskData);
    void delete_all_save_data_for_user(sys::threadpool::JobData taskData);
}
