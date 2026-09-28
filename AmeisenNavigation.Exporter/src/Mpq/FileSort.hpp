#pragma once

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>
#include <tuple>
#include <vector>

/// MPQ load order of the WotLK client, later archives override earlier ones:
///
///   base archives   common.MPQ, common-2.MPQ, expansion.MPQ, lichking.MPQ
///   locale base     <locale>/locale-<locale>.MPQ, expansion-locale-..., lichking-locale-..., speech...
///   patches         patch.MPQ, patch-2.MPQ, patch-3.MPQ, ... patch-9.MPQ, patch-A.MPQ ... patch-Z.MPQ
///   locale patches  <locale>/patch-<locale>.MPQ, patch-<locale>-2.MPQ, ...
///
/// Custom server patches (patch-4.MPQ, patch-X.MPQ, ...) therefore win over the stock archives.
struct MpqLoadOrder
{
    int tier;         // 0 base, 1 locale base, 2 patch, 3 locale patch
    int rank;         // order inside the tier
    std::string name; // lowercase file name, tie breaker

    auto operator<=>(const MpqLoadOrder&) const = default;
};

namespace MpqLoadOrderDetail {
inline std::string ToLower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

/// Rank of a patch suffix: "" -> 0, "2".."9" -> 2..9, "a".."z" -> 100..125, anything else -> 200.
inline int PatchSuffixRank(const std::string& suffix)
{
    if (suffix.empty())
    {
        return 0;
    }

    if (std::all_of(suffix.begin(), suffix.end(), [](unsigned char c) { return std::isdigit(c) != 0; }))
    {
        try
        {
            return std::min(std::stoi(suffix), 99);
        }
        catch (...)
        {
            return 99;
        }
    }

    if (suffix.size() == 1 && std::isalpha(static_cast<unsigned char>(suffix[0])))
    {
        return 100 + (suffix[0] - 'a');
    }

    return 200;
}
} // namespace MpqLoadOrderDetail

/// Compute the load order key of an archive. `isLocale` = the archive lives in a locale sub folder (e.g. Data/enUS).
inline MpqLoadOrder GetMpqLoadOrder(const std::filesystem::path& archive, bool isLocale)
{
    using namespace MpqLoadOrderDetail;

    const std::string stem = ToLower(archive.stem().string());
    const bool isPatch = stem.rfind("patch", 0) == 0;

    MpqLoadOrder order{0, 0, stem};

    if (isPatch)
    {
        order.tier = isLocale ? 3 : 2;

        // "patch", "patch-2", "patch-x", "patch-enus", "patch-enus-2"
        std::string suffix = stem.size() > 6 ? stem.substr(6) : std::string();

        if (isLocale)
        {
            const auto dash = suffix.find('-');
            suffix = dash == std::string::npos ? std::string() : suffix.substr(dash + 1);
        }

        order.rank = PatchSuffixRank(suffix);
        return order;
    }

    order.tier = isLocale ? 1 : 0;

    static const std::pair<const char*, int> knownBase[] = {
        {"common", 0}, {"common-2", 1}, {"expansion", 2}, {"lichking", 3},
    };

    order.rank = 10;

    for (const auto& [name, rank] : knownBase)
    {
        if (stem == name)
        {
            order.rank = rank;
        }
    }

    if (isLocale)
    {
        if (stem.rfind("locale-", 0) == 0)
            order.rank = 0;
        else if (stem.rfind("expansion-locale", 0) == 0)
            order.rank = 1;
        else if (stem.rfind("lichking-locale", 0) == 0)
            order.rank = 2;
    }

    return order;
}

/// Sort archives by descending priority (the archive that wins a lookup comes first).
/// `dataDir` is the game's Data folder, archives in sub folders count as locale archives.
inline void SortMpqsByPriority(std::vector<std::filesystem::path>& archives, const std::filesystem::path& dataDir)
{
    std::vector<std::pair<MpqLoadOrder, std::filesystem::path>> keyed;
    keyed.reserve(archives.size());

    std::error_code ec;
    const auto root = std::filesystem::weakly_canonical(dataDir, ec);

    for (const auto& archive : archives)
    {
        const auto parent = std::filesystem::weakly_canonical(archive, ec).parent_path();
        keyed.emplace_back(GetMpqLoadOrder(archive, parent != root), archive);
    }

    std::stable_sort(keyed.begin(), keyed.end(), [](const auto& a, const auto& b) { return b.first < a.first; });

    archives.clear();

    for (auto& [order, path] : keyed)
    {
        archives.push_back(std::move(path));
    }
}
