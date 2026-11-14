#pragma once
#include "build_options.hpp"
#include "sys/threadpool.hpp"

#if JKSV_ENABLE_UPDATER
namespace tasks::update
{
    /// @brief Checks for updates. If one is found, prompts to download it.
    void check_for_update(sys::threadpool::JobData jobData);

    /// @brief Downloads the new JKSV NRO.
    void download_update(sys::threadpool::JobData jobData);
}

#endif
