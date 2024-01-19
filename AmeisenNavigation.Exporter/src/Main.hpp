#pragma once

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <vector>

#define STORMLIB_NO_AUTO_LINK
#include <stormlib.h>

#include <shlwapi.h>
#pragma comment(lib, "Shlwapi.lib")

inline auto NaturalCompare(const std::filesystem::path& path1, const std::filesystem::path& path2) 
{
    return StrCmpLogicalW(path1.wstring().c_str(), path2.wstring().c_str());
}
