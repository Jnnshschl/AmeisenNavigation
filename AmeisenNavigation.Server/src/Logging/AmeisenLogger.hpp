#pragma once

#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>

#if defined WIN32 || defined WIN64
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

constexpr auto CONSOLE_COLOR_BLUE = 3;
constexpr auto CONSOLE_COLOR_GRAY = 8;
constexpr auto CONSOLE_COLOR_GREEN = 10;
constexpr auto CONSOLE_COLOR_RED = 12;
constexpr auto CONSOLE_COLOR_WHITE = 15;
constexpr auto CONSOLE_COLOR_YELLOW = 14;
#endif

#define LogI(...) Log(__FUNCTION__, CONSOLE_COLOR_BLUE, CONSOLE_COLOR_WHITE, __VA_ARGS__)
#define LogD(...) Log(__FUNCTION__, CONSOLE_COLOR_GRAY, CONSOLE_COLOR_WHITE, __VA_ARGS__)
#define LogW(...) Log(__FUNCTION__, CONSOLE_COLOR_YELLOW, CONSOLE_COLOR_WHITE, __VA_ARGS__)
#define LogE(...) Log(__FUNCTION__, CONSOLE_COLOR_RED, CONSOLE_COLOR_WHITE, __VA_ARGS__)
#define LogS(...) Log(__FUNCTION__, CONSOLE_COLOR_GREEN, CONSOLE_COLOR_WHITE, __VA_ARGS__)

static bool ConsoleOpened;
static _iobuf* StdoutHandle;
static std::ofstream LogFileStream;

template<typename ...Args>
constexpr void Log(const std::string& tag, int color, int colorSecond, Args&& ...args)
{
    static std::string Delimiter = " >> ";

#if defined WIN32 || defined WIN64
    static void* ConsoleHandle = GetStdHandle(STD_OUTPUT_HANDLE);
#endif

    std::cout << std::setw(8) << tag;

#if defined WIN32 || defined WIN64
    SetConsoleTextAttribute(ConsoleHandle, color);
#endif

    std::cout << Delimiter;

#if defined WIN32 || defined WIN64
    SetConsoleTextAttribute(ConsoleHandle, colorSecond);
#endif

    (std::cout << ... << args) << std::endl;
}