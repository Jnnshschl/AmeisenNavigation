#pragma once

#include <atomic>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <format>
#include <mutex>
#include <string>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

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

inline std::mutex& GetMutex()
{
    static std::mutex mtx;
    return mtx;
}

inline void Initialize()
{
#ifdef _WIN32
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hOut != INVALID_HANDLE_VALUE)
    {
        DWORD dwMode = 0;
        if (GetConsoleMode(hOut, &dwMode))
        {
            dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
            SetConsoleMode(hOut, dwMode);
        }
    }
#endif
}

inline const char* LevelTag(Level level)
{
    switch (level)
    {
        case Level::Info:     return "\033[96m[INFO ]\033[0m";
        case Level::Success:  return "\033[92m[OK   ]\033[0m";
        case Level::Warning:  return "\033[93m[WARN ]\033[0m";
        case Level::Error:    return "\033[91m[ERR  ]\033[0m";
        case Level::Debug:    return "\033[90m[DEBUG]\033[0m";
        case Level::Map:      return "\033[95m[MAP  ]\033[0m";
        case Level::Timer:    return "\033[94m[TIME ]\033[0m";
        case Level::Progress: return "\033[93m[PROG ]\033[0m";
        default:              return "[?????]";
    }
}

inline std::string GetTimestamp() noexcept
{
    try
    {
        auto now = std::chrono::system_clock::now();
        auto in_time_t = std::chrono::system_clock::to_time_t(now);
        std::tm bt;
        localtime_s(&bt, &in_time_t);
        return std::format("{:02}:{:02}:{:02}", bt.tm_hour, bt.tm_min, bt.tm_sec);
    }
    catch (...) { return "??:??:??"; }
}

/// Thread-safe log implementation: formats to a single string, writes under lock.
/// Named LogImpl to avoid overload-resolution ambiguity with the variadic template.
/// Safe to call from noexcept contexts - never throws.
inline void LogImpl(Level level, const std::string& msg) noexcept
{
    try
    {
        std::string timestamp = GetTimestamp();
        std::string line = std::format("\033[90m[{}] \033[0m{} {}\n", timestamp, LevelTag(level), msg);

        const std::lock_guard lock(GetMutex());
        fputs(line.c_str(), stdout);
        fflush(stdout);
    }
    catch (...) { /* swallow - cannot let logging crash noexcept callers */ }
}

/// Single-string overload - delegates to LogImpl.
inline void Log(Level level, const std::string& msg) noexcept
{
    LogImpl(level, msg);
}

/// Variadic convenience - concatenates args into one string, then logs.
/// Safe to call from noexcept contexts - never throws.
template <typename... Args>
inline void Log(Level level, Args&&... args) noexcept
{
    try
    {
        std::string msg;
        ((msg += std::format("{}", std::forward<Args>(args))), ...);
        LogImpl(level, msg);
    }
    catch (...) { /* swallow */ }
}

/// Overwrite-in-place progress line (no newline - uses \r).
/// Thread-safe. Stays on one line until a normal Log() call adds a newline.
inline void LogProgress(const std::string& msg) noexcept
{
    try
    {
        std::string timestamp = GetTimestamp();
        std::string line = std::format("\r\033[90m[{}] \033[0m{} {}\033[K", timestamp, LevelTag(Level::Progress), msg);

        const std::lock_guard lock(GetMutex());
        fputs(line.c_str(), stdout);
        fflush(stdout);
    }
    catch (...) {}
}

/// End a progress sequence: print a final newline so subsequent logs start on a fresh line.
inline void EndProgress()
{
    const std::lock_guard lock(GetMutex());
    fputs("\n", stdout);
    fflush(stdout);
}

/// Format seconds as "Xh Ym Zs" or "Ym Zs" or "Zs".
inline std::string FormatDuration(double seconds) noexcept
{
    try
    {
        if (seconds < 0.0)
            return "?";
        int s = static_cast<int>(seconds);
        if (s >= 3600)
            return std::format("{}h {:02}m {:02}s", s / 3600, (s % 3600) / 60, s % 60);
        if (s >= 60)
            return std::format("{}m {:02}s", s / 60, s % 60);
        return std::format("{}s", s);
    }
    catch (...) { return "?"; }
}

} // namespace Logger

// Global Macros for ease of use
#define LogI(...) Logger::Log(Logger::Level::Info, __VA_ARGS__)
#define LogS(...) Logger::Log(Logger::Level::Success, __VA_ARGS__)
#define LogW(...) Logger::Log(Logger::Level::Warning, __VA_ARGS__)
#define LogE(...) Logger::Log(Logger::Level::Error, __VA_ARGS__)
#define LogD(...) Logger::Log(Logger::Level::Debug, __VA_ARGS__)
#define LogP(msg) Logger::LogProgress(msg)
