#include "logging/logger.hpp"

#include "sys/startup.hpp"

#include <array>
#include <cstdarg>
#include <cstdio>

namespace
{
    /// @brief This is the buffer size for log strings.
    constexpr size_t VA_BUFFER_SIZE = 0x1000;
} // namespace

void logger::initialize()
{
    // The shared raw-FS log is opened from the NRO's argv path before C++ constructors.
}

void logger::log(const char *format, ...) noexcept
{
    std::array<char, VA_BUFFER_SIZE> vaBuffer = {0};
    std::va_list vaList{};
    va_start(vaList, format);
    vsnprintf(vaBuffer.data(), VA_BUFFER_SIZE, format, vaList);
    va_end(vaList);
    sys::boot_log("%s", vaBuffer.data());
}
