#pragma once
#include "build_options.hpp"
#include "appstates/BackupMenuState.hpp"
#include "sys/sys.hpp"

#include <string>

namespace tasks::backup
{
    /// @brief Task/thread function executed when a new backup is created.
    void create_new_backup_local(sys::threadpool::JobData taskData);
#if JKSV_ENABLE_REMOTE
    void create_new_backup_remote(sys::threadpool::JobData taskData);
#endif

    /// @brief Overwrites a pre-existing backup.
    void overwrite_backup_local(sys::threadpool::JobData taskData);
#if JKSV_ENABLE_REMOTE
    void overwrite_backup_remote(sys::threadpool::JobData taskData);
#endif

    /// @brief Restores a backup
    void restore_backup_local(sys::threadpool::JobData taskData);
#if JKSV_ENABLE_REMOTE
    void restore_backup_remote(sys::threadpool::JobData taskData);
#endif

    /// @brief Deletes a backup
    void delete_backup_local(sys::threadpool::JobData taskData);
#if JKSV_ENABLE_REMOTE
    void delete_backup_remote(sys::threadpool::JobData taskData);
#endif

    /// @brief Uploads a backup
#if JKSV_ENABLE_REMOTE
    void upload_backup(sys::threadpool::JobData taskData);
#endif

    /// @brief Patches a pre-existing backup on the remote storage.
#if JKSV_ENABLE_REMOTE
    void patch_backup(sys::threadpool::JobData taskData);
#endif
}
