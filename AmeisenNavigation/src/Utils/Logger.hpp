#pragma once

#include <chrono>
#include <ctime>
#include <format>
#include <iostream>
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
    Timer
};

static void Initialize()
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

static std::string GetTimestamp()
{
    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);
    std::tm bt;
    localtime_s(&bt, &in_time_t);
    return std::format("{:02}:{:02}:{:02}", bt.tm_hour, bt.tm_min, bt.tm_sec);
}

template <typename... Args>
static void InternalLog(const std::string& levelStr, const std::string& color, Args&&... args)
{
    std::string timestamp = GetTimestamp();
    std::cout << "\033[90m[" << timestamp << "] \033[0m" << color << "[" << levelStr << "]\033[0m ";
    (std::cout << ... << args) << std::endl;
}

template <typename... Args>
static void Log(Level level, Args&&... args)
{
    switch (level)
    {
        case Level::Info:
            InternalLog("INFO ", "\033[96m", std::forward<Args>(args)...);
            break;
        case Level::Success:
            InternalLog("OK   ", "\033[92m", std::forward<Args>(args)...);
            break;
        case Level::Warning:
            InternalLog("WARN ", "\033[93m", std::forward<Args>(args)...);
            break;
        case Level::Error:
            InternalLog("ERR  ", "\033[91m", std::forward<Args>(args)...);
            break;
        case Level::Debug:
            InternalLog("DEBUG", "\033[90m", std::forward<Args>(args)...);
            break;
        case Level::Map:
            InternalLog("MAP  ", "\033[95m", std::forward<Args>(args)...); // Magenta
            break;
        case Level::Timer:
            InternalLog("TIME ", "\033[94m", std::forward<Args>(args)...); // Blue
            break;
    }
}
} // namespace Logger

// Global Macros for ease of use
#define LogI(...) Logger::Log(Logger::Level::Info, __VA_ARGS__)
#define LogS(...) Logger::Log(Logger::Level::Success, __VA_ARGS__)
#define LogW(...) Logger::Log(Logger::Level::Warning, __VA_ARGS__)
#define LogE(...) Logger::Log(Logger::Level::Error, __VA_ARGS__)
#define LogD(...) Logger::Log(Logger::Level::Debug, __VA_ARGS__)
