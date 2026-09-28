#pragma once

#include <atomic>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <format>
#include <iterator>
#include <mutex>
#include <string>
#include <string_view>
#include <type_traits>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <io.h>
#include <windows.h>
#else
#include <unistd.h>
#endif

/// Minimal thread-safe console logger shared by the server, exporter and library.
///
/// - Every call formats into one string and writes it under a single lock, so lines never interleave.
/// - Logging never throws, it is safe to use from noexcept code.
/// - Debug output is filtered at runtime *before* any formatting happens (see LogD), so it is free when disabled.
/// - ANSI colors are only emitted when stdout is an interactive terminal.
namespace Logger {
enum class Level
{
    Info,
    Success,
    Warning,
    Error,
    Debug,
    Map,
    Timer,
    Progress
};

namespace Detail {
inline std::mutex& Mutex() noexcept
{
    static std::mutex mtx;
    return mtx;
}

inline std::atomic<bool>& DebugFlag() noexcept
{
#ifdef _DEBUG
    static std::atomic<bool> enabled{true};
#else
    static std::atomic<bool> enabled{false};
#endif
    return enabled;
}

inline std::atomic<bool>& ColorFlag() noexcept
{
    static std::atomic<bool> enabled{true};
    return enabled;
}

inline const char* LevelTag(Level level, bool color) noexcept
{
    switch (level)
    {
        case Level::Info:     return color ? "\033[96m[INFO ]\033[0m" : "[INFO ]";
        case Level::Success:  return color ? "\033[92m[OK   ]\033[0m" : "[OK   ]";
        case Level::Warning:  return color ? "\033[93m[WARN ]\033[0m" : "[WARN ]";
        case Level::Error:    return color ? "\033[91m[ERR  ]\033[0m" : "[ERR  ]";
        case Level::Debug:    return color ? "\033[90m[DEBUG]\033[0m" : "[DEBUG]";
        case Level::Map:      return color ? "\033[95m[MAP  ]\033[0m" : "[MAP  ]";
        case Level::Timer:    return color ? "\033[94m[TIME ]\033[0m" : "[TIME ]";
        case Level::Progress: return color ? "\033[93m[PROG ]\033[0m" : "[PROG ]";
        default:              return "[?????]";
    }
}

inline std::string Timestamp() noexcept
{
    try
    {
        const auto now = std::chrono::system_clock::now();
        const auto seconds = std::chrono::system_clock::to_time_t(now);
        const auto millis =
            std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000;

        std::tm bt{};
#ifdef _WIN32
        localtime_s(&bt, &seconds);
#else
        localtime_r(&seconds, &bt);
#endif
        return std::format("{:02}:{:02}:{:02}.{:03}", bt.tm_hour, bt.tm_min, bt.tm_sec, millis);
    }
    catch (...)
    {
        return "??:??:??.???";
    }
}

inline void Write(std::string_view line) noexcept
{
    const std::lock_guard lock(Mutex());
    std::fwrite(line.data(), 1, line.size(), stdout);
    std::fflush(stdout);
}
} // namespace Detail

/// Enable ANSI colors on Windows consoles and disable them when stdout is redirected.
inline void Initialize() noexcept
{
#ifdef _WIN32
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD dwMode = 0;
    const bool isConsole = hOut != INVALID_HANDLE_VALUE && GetConsoleMode(hOut, &dwMode);

    if (isConsole)
    {
        SetConsoleMode(hOut, dwMode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
        SetConsoleOutputCP(CP_UTF8);
    }

    Detail::ColorFlag().store(isConsole, std::memory_order_relaxed);
#else
    Detail::ColorFlag().store(isatty(fileno(stdout)) != 0, std::memory_order_relaxed);
#endif
}

inline void SetDebugEnabled(bool enabled) noexcept { Detail::DebugFlag().store(enabled, std::memory_order_relaxed); }
inline bool IsDebugEnabled() noexcept { return Detail::DebugFlag().load(std::memory_order_relaxed); }
inline void SetColorsEnabled(bool enabled) noexcept { Detail::ColorFlag().store(enabled, std::memory_order_relaxed); }
inline bool IsColorEnabled() noexcept { return Detail::ColorFlag().load(std::memory_order_relaxed); }

inline bool IsEnabled(Level level) noexcept { return level != Level::Debug || IsDebugEnabled(); }

/// Log a pre-formatted message.
inline void LogImpl(Level level, std::string_view msg) noexcept
{
    try
    {
        const bool color = IsColorEnabled();
        Detail::Write(std::format("{}[{}] {}{} {}\n", color ? "\033[90m" : "", Detail::Timestamp(),
                                  color ? "\033[0m" : "", Detail::LevelTag(level, color), msg));
    }
    catch (...)
    {
        // Logging must never take down a noexcept caller.
    }
}

/// Concatenate all arguments (each formatted with "{}") and log them as one line.
template <typename... Args>
inline void Log(Level level, const Args&... args) noexcept
{
    if (!IsEnabled(level))
    {
        return;
    }

    try
    {
        if constexpr (sizeof...(Args) == 1 && (std::is_convertible_v<const Args&, std::string_view> && ...))
        {
            LogImpl(level, std::string_view(args...));
        }
        else
        {
            std::string msg;
            (std::format_to(std::back_inserter(msg), "{}", args), ...);
            LogImpl(level, msg);
        }
    }
    catch (...)
    {
    }
}

/// Overwrite-in-place progress line (uses \r, no newline). Falls back to normal lines when not on a terminal.
inline void LogProgress(std::string_view msg) noexcept
{
    try
    {
        if (!IsColorEnabled())
        {
            LogImpl(Level::Progress, msg);
            return;
        }

        Detail::Write(std::format("\r\033[90m[{}] \033[0m{} {}\033[K", Detail::Timestamp(),
                                  Detail::LevelTag(Level::Progress, true), msg));
    }
    catch (...)
    {
    }
}

/// End a progress sequence so subsequent logs start on a fresh line.
inline void EndProgress() noexcept
{
    if (IsColorEnabled())
    {
        Detail::Write("\n");
    }
}

/// Format seconds as "Xh Ym Zs", "Ym Zs" or "Zs".
inline std::string FormatDuration(double seconds) noexcept
{
    try
    {
        if (seconds < 0.0)
        {
            return "?";
        }

        const auto s = static_cast<long long>(seconds);

        if (s >= 3600)
        {
            return std::format("{}h {:02}m {:02}s", s / 3600, (s % 3600) / 60, s % 60);
        }

        if (s >= 60)
        {
            return std::format("{}m {:02}s", s / 60, s % 60);
        }

        return std::format("{}s", s);
    }
    catch (...)
    {
        return "?";
    }
}
} // namespace Logger

// Global macros. LogD checks the runtime debug flag before evaluating its arguments.
#define LogI(...) ::Logger::Log(::Logger::Level::Info, __VA_ARGS__)
#define LogS(...) ::Logger::Log(::Logger::Level::Success, __VA_ARGS__)
#define LogW(...) ::Logger::Log(::Logger::Level::Warning, __VA_ARGS__)
#define LogE(...) ::Logger::Log(::Logger::Level::Error, __VA_ARGS__)
#define LogD(...)                                                                                                      \
    do                                                                                                                 \
    {                                                                                                                  \
        if (::Logger::IsDebugEnabled())                                                                                \
            ::Logger::Log(::Logger::Level::Debug, __VA_ARGS__);                                                        \
    } while (0)
#define LogP(msg) ::Logger::LogProgress(msg)
