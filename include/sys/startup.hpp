#pragma once

namespace sys
{
    enum class LogStatus
    {
        Unavailable,
        Ready,
        MissingNroPath,
        InvalidNroPath,
        OpenFailed,
        WriteFailed,
        Closed
    };

    struct LogInfo
    {
        LogStatus status;
        unsigned int error;
        // UTF-8 paths remain valid for the program lifetime; unknown paths are empty strings.
        const char *nroPath;
        const char *logPath;
    };

#ifdef __SWITCH2__
    // Independent raw-FS log, available before C++ global constructors and fsdev mounts.
    void boot_log(const char *format, ...) noexcept __attribute__((format(printf, 1, 2)));
    LogInfo get_log_info() noexcept;
#else
    inline void boot_log(const char *, ...) noexcept {}
    inline LogInfo get_log_info() noexcept { return {LogStatus::Unavailable, 0, "", ""}; }
#endif
}
