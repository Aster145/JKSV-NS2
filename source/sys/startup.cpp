#include "sys/startup.hpp"

#ifdef __SWITCH2__
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <cstddef>
#include <sys/reent.h>
#include <switch.h>

extern "C"
{
    bool __libnx_hosversion_is_set(void);
    void __libnx_init_time(void);
    void __nx_win_init(void) __attribute__((weak));
    void __nx_win_exit(void) __attribute__((weak));
    void __nx_gpu_exit(void) __attribute__((weak));
    void userAppInit(void) __attribute__((weak));
    void userAppExit(void) __attribute__((weak));
    extern u32 __nx_applet_type;
    extern int __system_argc;
    extern char **__system_argv;
}

namespace
{
    // These remain valid before C++ constructors and after C++ destructors.
    constinit FsFileSystem s_logFs{};
    constinit FsFile s_logFile{};
    constinit Mutex s_logMutex{};
    constinit s64 s_logOffset{};
    constinit bool s_logOpen{};
    constinit bool s_logWritable{};
    constinit bool s_sdMounted{};
    constinit sys::LogStatus s_logStatus = sys::LogStatus::Unavailable;
    constinit Result s_logError{};
    constexpr char LOG_FILENAME[] = "JKSV-NS2.log";
    constexpr size_t DEVICE_PREFIX_LENGTH = sizeof("sdmc:") - 1;
    constinit char s_nroPath[FS_MAX_PATH + DEVICE_PREFIX_LENGTH]{};
    constinit char s_logPath[FS_MAX_PATH + DEVICE_PREFIX_LENGTH]{};

    // argvSetup has already split/removed argument quotes. Keep UTF-8 and spaces intact.
    // Relative paths have no reliable base because this application deliberately skips cwd setup.
    sys::LogStatus prepare_log_paths(const char *argument, char *nroPath, char *logPath) noexcept
    {
        if (!argument || !argument[0]) { return sys::LogStatus::MissingNroPath; }
        const char *path = argument;
        if (std::strncmp(path, "sdmc:", DEVICE_PREFIX_LENGTH) == 0) { path += DEVICE_PREFIX_LENGTH; }
        if (path[0] != '/') { return sys::LogStatus::InvalidNroPath; }

        size_t length = 0;
        while (length < FS_MAX_PATH && path[length]) { ++length; }
        if (length >= FS_MAX_PATH || path[length - 1] == '/') { return sys::LogStatus::InvalidNroPath; }
        const char *lastSlash = path;
        const char *component = path + 1;
        for (size_t i = 1; i <= length; ++i)
        {
            const unsigned char character = static_cast<unsigned char>(path[i]);
            if (character && (character < 0x20 || character == 0x7F || character == '\\' || character == ':'))
            {
                return sys::LogStatus::InvalidNroPath;
            }
            if (character != '/' && character != 0) { continue; }
            const size_t componentLength = path + i - component;
            if (componentLength == 0 || (componentLength == 1 && component[0] == '.') ||
                (componentLength == 2 && component[0] == '.' && component[1] == '.'))
            {
                return sys::LogStatus::InvalidNroPath;
            }
            if (character == '/')
            {
                lastSlash = path + i;
                component = path + i + 1;
            }
        }

        // Never truncate the running NRO if it was unusually named like the log (FAT is case-insensitive).
        const size_t filenameLength = path + length - component;
        bool sameFilename = filenameLength == sizeof(LOG_FILENAME) - 1;
        for (size_t i = 0; sameFilename && i < filenameLength; ++i)
        {
            unsigned char actual = static_cast<unsigned char>(component[i]);
            unsigned char expected = static_cast<unsigned char>(LOG_FILENAME[i]);
            if (actual >= 'A' && actual <= 'Z') { actual += 'a' - 'A'; }
            if (expected >= 'A' && expected <= 'Z') { expected += 'a' - 'A'; }
            sameFilename = actual == expected;
        }
        if (sameFilename) { return sys::LogStatus::InvalidNroPath; }

        const size_t directoryLength = lastSlash - path + 1;
        if (directoryLength + sizeof(LOG_FILENAME) > FS_MAX_PATH) { return sys::LogStatus::InvalidNroPath; }
        std::memcpy(nroPath, "sdmc:", DEVICE_PREFIX_LENGTH);
        std::memcpy(nroPath + DEVICE_PREFIX_LENGTH, path, length + 1);
        std::memcpy(logPath, "sdmc:", DEVICE_PREFIX_LENGTH);
        std::memcpy(logPath + DEVICE_PREFIX_LENGTH, path, directoryLength);
        std::memcpy(logPath + DEVICE_PREFIX_LENGTH + directoryLength, LOG_FILENAME, sizeof(LOG_FILENAME));
        return sys::LogStatus::Ready;
    }

    Result open_boot_log()
    {
        const char *nroArgument = __system_argc > 0 && __system_argv ? __system_argv[0] : nullptr;
        s_logStatus = prepare_log_paths(nroArgument, s_nroPath, s_logPath);
        if (s_logStatus != sys::LogStatus::Ready)
        {
            return MAKERESULT(Module_Libnx, LibnxError_BadInput);
        }

        s_logStatus = sys::LogStatus::OpenFailed;
        Result result = fsOpenSdCardFileSystem(&s_logFs);
        if (R_FAILED(result))
        {
            s_logError = result;
            return result;
        }
        const char *path = s_logPath + DEVICE_PREFIX_LENGTH;
        result = fsFsOpenFile(&s_logFs, path, FsOpenMode_Write | FsOpenMode_Append, &s_logFile);
        if (R_FAILED(result))
        {
            result = fsFsCreateFile(&s_logFs, path, 0, 0);
            if (R_SUCCEEDED(result))
            {
                result = fsFsOpenFile(&s_logFs, path, FsOpenMode_Write | FsOpenMode_Append, &s_logFile);
            }
        }
        if (R_FAILED(result))
        {
            fsFsClose(&s_logFs);
            s_logError = result;
            return result;
        }
        result = fsFileSetSize(&s_logFile, 0);
        if (R_FAILED(result))
        {
            fsFileClose(&s_logFile);
            fsFsClose(&s_logFs);
            s_logError = result;
            return result;
        }
        s_logOpen = true;
        s_logWritable = true;
        s_logStatus = sys::LogStatus::Ready;
        return result;
    }

    void log_result(const char *name, Result result)
    {
        sys::boot_log("END %s: rc=0x%08X module=%u description=%u", name,
                      static_cast<unsigned>(result), static_cast<unsigned>(R_MODULE(result)),
                      static_cast<unsigned>(R_DESCRIPTION(result)));
    }

    void require_success(const char *name, Result result)
    {
        log_result(name, result);
        if (R_FAILED(result))
        {
            sys::boot_log("STARTUP_FAILED: %s", name);
            // Returning from __appInit would run FsLib constructors without their services.
            diagAbortWithResult(result);
        }
    }
}

void sys::boot_log(const char *format, ...) noexcept
{
    mutexLock(&s_logMutex);
    if (s_logWritable)
    {
        char text[0x1000 + 2];
        std::va_list args;
        va_start(args, format);
        const int count = vsnprintf(text, sizeof(text) - 1, format, args);
        va_end(args);
        if (count >= 0)
        {
            size_t length = static_cast<size_t>(count);
            if (length > sizeof(text) - 2) { length = sizeof(text) - 2; }
            text[length++] = '\n';
            const Result result = fsFileWrite(&s_logFile, s_logOffset, text, length, FsWriteOption_Flush);
            if (R_SUCCEEDED(result)) { s_logOffset += length; }
            else
            {
                s_logWritable = false;
                s_logStatus = sys::LogStatus::WriteFailed;
                s_logError = result;
            }
        }
    }
    mutexUnlock(&s_logMutex);
}

sys::LogInfo sys::get_log_info() noexcept
{
    mutexLock(&s_logMutex);
    const LogInfo info{s_logStatus, s_logError, s_nroPath, s_logPath};
    mutexUnlock(&s_logMutex);
    return info;
}

extern "C" void __appInit(void)
{
    Result result = smInitialize();
    if (R_FAILED(result)) { diagAbortWithResult(result); }
    result = fsInitialize();
    if (R_FAILED(result)) { diagAbortWithResult(result); }
    // Independent FS handles survive FsLib replacing the SDK's sdmc devoptab.
    open_boot_log();
    sys::boot_log("JKSV-NS2 1.0.0");
    sys::boot_log("NRO: %s", s_nroPath);
    sys::boot_log("Log: %s", s_logPath);
    sys::boot_log("SM_INIT=OK\nFS_INIT=OK");
    sys::boot_log("ABI: sizeof_reent=%u deviceData_offset=0x%X",
                  static_cast<unsigned>(sizeof(struct _reent)),
                  static_cast<unsigned>(offsetof(struct _reent, deviceData)));
    sys::boot_log("ENV: hos=0x%08X hos_set=%u applet_type=%d", static_cast<unsigned>(hosversionGet()),
                  static_cast<unsigned>(__libnx_hosversion_is_set()), static_cast<int>(__nx_applet_type));

    if (!__libnx_hosversion_is_set())
    {
        sys::boot_log("BEGIN setsysInitialize (optional version query)");
        result = setsysInitialize();
        log_result("setsysInitialize", result);
        if (R_SUCCEEDED(result))
        {
            SetSysFirmwareVersion firmware{};
            sys::boot_log("BEGIN setsysGetFirmwareVersion");
            result = setsysGetFirmwareVersion(&firmware);
            log_result("setsysGetFirmwareVersion", result);
            if (R_SUCCEEDED(result)) { hosversionSet(MAKEHOSVERSION(firmware.major, firmware.minor, firmware.micro)); }
            setsysExit();
        }
    }

    sys::boot_log("BEGIN appletInitialize");
    require_success("appletInitialize", appletInitialize());
    if (static_cast<AppletType>(__nx_applet_type) != AppletType_None)
    {
        sys::boot_log("BEGIN hidInitialize");
        require_success("hidInitialize", hidInitialize());
    }
    sys::boot_log("BEGIN timeInitialize");
    require_success("timeInitialize", timeInitialize());
    sys::boot_log("BEGIN __libnx_init_time");
    __libnx_init_time();
    sys::boot_log("END __libnx_init_time");
    sys::boot_log("BEGIN fsdevMountSdmc");
    result = fsdevMountSdmc();
    s_sdMounted = R_SUCCEEDED(result);
    log_result("fsdevMountSdmc", result);

    // MinTest 06 stopped inside this helper. Tests 07-09 passed with it skipped.
    // JKSV's active paths are absolute (sdmc:/ and romfs:/); its updater is disabled.
    sys::boot_log("SKIP __libnx_init_cwd");
    if (__nx_win_init)
    {
        sys::boot_log("BEGIN __nx_win_init");
        __nx_win_init();
        sys::boot_log("END __nx_win_init");
    }
    if (userAppInit) { userAppInit(); }
    sys::boot_log("END __appInit; C++ constructors follow");
}

extern "C" void __appExit(void)
{
    sys::boot_log("BEGIN __appExit; C++ destructors finished");
    if (userAppExit) { userAppExit(); }
    if (__nx_gpu_exit)
    {
        sys::boot_log("BEGIN __nx_gpu_exit");
        __nx_gpu_exit();
        sys::boot_log("END __nx_gpu_exit");
    }
    if (__nx_win_exit)
    {
        sys::boot_log("BEGIN __nx_win_exit");
        __nx_win_exit();
        sys::boot_log("END __nx_win_exit");
    }
    if (s_sdMounted)
    {
        sys::boot_log("BEGIN fsdevUnmountAll");
        log_result("fsdevUnmountAll", fsdevUnmountAll());
    }
    sys::boot_log("BEGIN timeExit");
    timeExit();
    sys::boot_log("END timeExit");
    if (static_cast<AppletType>(__nx_applet_type) != AppletType_None)
    {
        sys::boot_log("BEGIN hidExit");
        hidExit();
        sys::boot_log("END hidExit");
    }
    sys::boot_log("BEGIN appletExit");
    appletExit();
    sys::boot_log("END appletExit");
    sys::boot_log("CLOSING_LOG_AND_FS_THEN_RETURN");
    mutexLock(&s_logMutex);
    s_logWritable = false;
    if (s_logStatus == sys::LogStatus::Ready) { s_logStatus = sys::LogStatus::Closed; }
    if (s_logOpen)
    {
        fsFileClose(&s_logFile);
        fsFsClose(&s_logFs);
        s_logOpen = false;
    }
    mutexUnlock(&s_logMutex);
    fsExit();
    smExit();
}
#endif
