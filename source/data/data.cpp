#include "data/data.hpp"

#include "appstates/DataLoadingState.hpp"
#include "appstates/FadeState.hpp"
#include "data/DataContext.hpp"
#include "error.hpp"
#include "logging/logger.hpp"
#include "strings/strings.hpp"
#include "sys/startup.hpp"

#include <switch.h>

namespace
{
    // clang-format off
    // This seems stupid, but it's the only way, really.
    struct StateDataStruct : sys::Task::DataStruct
    {
        bool clearCache{};
    };
    // clang-format on

    data::DataContext s_context{};
} // namespace

/// @brief The main routine for the task to load data.
static void data_initialize_task(sys::threadpool::JobData taskData);

void data::launch_initialization(bool clearCache, std::function<void()> onDestruction)
{
    sys::boot_log("BEGIN data loading state: clear_cache=%u", static_cast<unsigned>(clearCache));
    auto taskData        = std::make_shared<StateDataStruct>();
    taskData->clearCache = clearCache;

    sys::boot_log("BEGIN DataLoadingState::create");
    auto loadingState = DataLoadingState::create(s_context, onDestruction, data_initialize_task, taskData);
    sys::boot_log("END DataLoadingState::create");
    sys::boot_log("BEGIN StateManager::push_state loading");
    StateManager::push_state(loadingState);
    sys::boot_log("END StateManager::push_state loading");
    sys::boot_log("END data loading state");
}

void data::get_users(data::UserList &userList) { s_context.get_users(userList); }

data::TitleInfo *data::get_title_info_by_id(uint64_t applicationID) noexcept
{
    return s_context.get_title_by_id(applicationID);
}

void data::load_title_to_map(uint64_t applicationID) { s_context.load_title(applicationID); }

bool data::title_exists_in_map(uint64_t applicationID) noexcept { return s_context.title_is_loaded(applicationID); }

void data::get_title_info_list(data::TitleInfoList &listOut) { s_context.get_title_info_list(listOut); }

void data::get_title_info_by_type(FsSaveDataType saveType, data::TitleInfoList &listOut)
{
    s_context.get_title_info_list_by_type(saveType, listOut);
}

static void data_initialize_task(sys::threadpool::JobData taskData)
{
    sys::boot_log("BEGIN data background initialization");
    auto castData         = std::static_pointer_cast<StateDataStruct>(taskData);
    sys::Task *task       = castData->task;
    const bool clearCache = castData->clearCache;

    if (error::is_null(task))
    {
        sys::boot_log("END data background initialization: null task");
        return;
    }
    const char *statusFinalizing = strings::get_by_name(strings::names::DATA_LOADING_STATUS, 6);

    if (clearCache)
    {
        sys::boot_log("BEGIN data delete_cache");
        s_context.delete_cache();
        sys::boot_log("END data delete_cache");
    }
    sys::boot_log("BEGIN data 1/6 read_cache");
    const bool cacheRead = s_context.read_cache(task);
    sys::boot_log("END data 1/6 read_cache: ok=%u", static_cast<unsigned>(cacheRead));
    sys::boot_log("BEGIN data 2/6 load_application_records");
    s_context.load_application_records(task);
    sys::boot_log("END data 2/6 load_application_records");
    sys::boot_log("BEGIN data 3/6 import_svi_files");
    s_context.import_svi_files(task);
    sys::boot_log("END data 3/6 import_svi_files");
    sys::boot_log("BEGIN data 4/6 load_create_users");
    const bool usersLoaded = s_context.load_create_users(task);
    sys::boot_log("END data 4/6 load_create_users: ok=%u", static_cast<unsigned>(usersLoaded));
    sys::boot_log("BEGIN data 5/6 load_user_save_info");
    s_context.load_user_save_info(task);
    sys::boot_log("END data 5/6 load_user_save_info");
    sys::boot_log("BEGIN data 6/6 write_cache");
    const bool cacheWritten = s_context.write_cache(task);
    sys::boot_log("END data 6/6 write_cache: ok=%u", static_cast<unsigned>(cacheWritten));

    sys::boot_log("BEGIN data task completion");
    task->set_status(statusFinalizing); // This is here so at least they know something is happening instead of a freeze.
    task->complete();
    sys::boot_log("END data task completion");
    sys::boot_log("END data background initialization");
}
