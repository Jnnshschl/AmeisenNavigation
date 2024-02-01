#pragma once

#include <filesystem>
#include <cwctype>

inline std::pair<std::wstring, int> ExtractNumber(const std::wstring& s)
{
    std::wstring prefix;
    int number = 0;
    bool foundDigit = false;

    for (const wchar_t& c : s)
    {
        if (std::iswdigit(c))
        {
            prefix += L' ';
            foundDigit = true;
            number = number * 10 + (c - L'0');
        }
        else
        {
            if (foundDigit)
            {
                break;
            }

            prefix += c;
        }
    }

    return { prefix, number };
}

inline int GetPathDepth(const std::filesystem::path& path, int depth = 0)
{
    if (path.has_parent_path())
    {
        const auto parent = path.parent_path();

        if (parent != path)
        {
            return GetPathDepth(parent, depth + 1);
        }
    }

    return depth;
}

/// <summary>
/// Try to sort the MPQ files in the order wow loads them.
/// </summary>
inline bool NaturalCompare(const std::filesystem::directory_entry& a, const std::filesystem::directory_entry& b)
{
    if (GetPathDepth(a.path()) > GetPathDepth(b.path()))
    {
        return true;
    }

    auto [prefixA, numberA] = ExtractNumber(a.path().filename().wstring());
    auto [prefixB, numberB] = ExtractNumber(b.path().filename().wstring());

    if (numberA == 0 && numberB != 0)
    {
        return true;
    }

    if (prefixA != prefixB)
    {
        return prefixA < prefixB;
    }

    return numberA < numberB;
}
