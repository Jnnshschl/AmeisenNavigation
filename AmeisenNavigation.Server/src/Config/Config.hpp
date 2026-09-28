#pragma once

#include <charconv>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

/// Type-safe configuration entry: wraps a reference to the actual config field.
using ConfigRef = std::variant<std::reference_wrapper<bool>, std::reference_wrapper<int>,
                               std::reference_wrapper<float>, std::reference_wrapper<std::string>>;

/// Server configuration, stored as "key=value" lines. Key prefix convention: b=bool, i=int, f=float, s=string.
/// Unknown keys are ignored, missing keys keep their defaults, lines starting with '#' or ';' are comments.
struct AmeisenNavConfig
{
    // ── Configuration Fields ─────────────────────────────────────────

    bool debugLogging = false;
    bool useAnpFileFormat = false;
    float catmullRomSplineAlpha = 0.5f;
    float factionDangerCost = 3.0f;
    float randomPathMaxDistance = 1.0f;
    int bezierCurvePoints = 8;
    int catmullRomSplinePoints = 4;
    int maxPointPath = 512;
    int maxPolyPath = 2048;
    int maxSearchNodes = 65535;
    int mmapFormat = 0; // -1 = custom patterns, 0 = auto detect, 1 = TrinityCore 3.3.5a, 2 = SkyFire 5.4.8
    int port = 47110;
    std::string customMmapPattern = "{:03}.mmap";
    std::string customMmtilePattern = "{:03}{:02}{:02}.mmtile";
    std::string ip = "127.0.0.1";
#ifdef _WIN32
    std::string mmapsPath = "C:\\meshes\\";
#else
    std::string mmapsPath = "./meshes/";
#endif
    std::string preloadMaps = ""; // comma separated map ids loaded at startup, e.g. "0,1,530,571"

    // ── Serialization ────────────────────────────────────────────────

    bool Save(const std::filesystem::path& path)
    {
        std::ofstream out(path, std::ios::trunc);

        if (!out.is_open())
        {
            return false;
        }

        out << "# AmeisenNavigation.Server configuration (key=value), see README.md for all options\n";

        for (const auto& [key, ref] : GetFieldMap())
        {
            std::visit(
                [&](auto&& r) {
                    using T = std::decay_t<decltype(r.get())>;

                    if constexpr (std::is_same_v<T, bool>)
                    {
                        out << key << '=' << (r.get() ? 1 : 0) << '\n';
                    }
                    else if constexpr (std::is_same_v<T, float>)
                    {
                        char buffer[64]{};
                        const auto [ptr, ec] = std::to_chars(buffer, buffer + sizeof(buffer), r.get());
                        out << key << '=' << std::string_view(buffer, ec == std::errc() ? ptr - buffer : 0) << '\n';
                    }
                    else
                    {
                        out << key << '=' << r.get() << '\n';
                    }
                },
                ref);
        }

        return out.good();
    }

    /// Load values from a file. Returns false if the file couldn't be opened.
    /// Malformed values are skipped and reported through `errors` (if given).
    bool Load(const std::filesystem::path& path, std::vector<std::string>* errors = nullptr)
    {
        std::ifstream in(path);

        if (!in.is_open())
        {
            return false;
        }

        auto fields = GetFieldMap();

        for (std::string line; std::getline(in, line);)
        {
            const std::string_view trimmedLine = Trim(line);

            if (trimmedLine.empty() || trimmedLine.front() == '#' || trimmedLine.front() == ';')
            {
                continue;
            }

            const auto delim = trimmedLine.find('=');

            if (delim == std::string_view::npos)
            {
                continue;
            }

            const std::string key(Trim(trimmedLine.substr(0, delim)));
            const std::string_view value = Trim(trimmedLine.substr(delim + 1));
            const auto it = fields.find(key);

            if (it == fields.end())
            {
                continue;
            }

            const bool ok = std::visit(
                [&](auto&& r) -> bool {
                    using T = std::decay_t<decltype(r.get())>;

                    if constexpr (std::is_same_v<T, bool>)
                    {
                        if (value == "true" || value == "True" || value == "TRUE")
                        {
                            r.get() = true;
                            return true;
                        }

                        if (value == "false" || value == "False" || value == "FALSE")
                        {
                            r.get() = false;
                            return true;
                        }

                        int v = 0;
                        const auto [ptr, ec] = std::from_chars(value.data(), value.data() + value.size(), v);

                        if (ec != std::errc() || ptr != value.data() + value.size())
                        {
                            return false;
                        }

                        r.get() = v > 0;
                        return true;
                    }
                    else if constexpr (std::is_same_v<T, int>)
                    {
                        int v = 0;
                        const auto [ptr, ec] = std::from_chars(value.data(), value.data() + value.size(), v);

                        if (ec != std::errc() || ptr != value.data() + value.size())
                        {
                            return false;
                        }

                        r.get() = v;
                        return true;
                    }
                    else if constexpr (std::is_same_v<T, float>)
                    {
                        // std::stof accepts "1.0f"-style leftovers, be strict instead.
                        float v = 0.0f;
                        const auto [ptr, ec] = std::from_chars(value.data(), value.data() + value.size(), v);

                        if (ec != std::errc() || ptr != value.data() + value.size() || !std::isfinite(v))
                        {
                            return false;
                        }

                        r.get() = v;
                        return true;
                    }
                    else
                    {
                        r.get() = std::string(value);
                        return true;
                    }
                },
                it->second);

            if (!ok && errors)
            {
                errors->push_back(key + " has an invalid value: \"" + std::string(value) + "\"");
            }
        }

        return true;
    }

    /// Parse sPreloadMaps ("0, 1,530") into map ids, invalid entries are skipped.
    std::vector<int> GetPreloadMaps() const
    {
        std::vector<int> result;
        std::string_view rest = preloadMaps;

        while (!rest.empty())
        {
            const auto comma = rest.find(',');
            const std::string_view token = Trim(rest.substr(0, comma));
            int id = 0;

            if (!token.empty())
            {
                const auto [ptr, ec] = std::from_chars(token.data(), token.data() + token.size(), id);

                if (ec == std::errc() && ptr == token.data() + token.size() && id >= 0)
                {
                    result.push_back(id);
                }
            }

            if (comma == std::string_view::npos)
            {
                break;
            }

            rest = rest.substr(comma + 1);
        }

        return result;
    }

private:
    static std::string_view Trim(std::string_view s) noexcept
    {
        constexpr std::string_view whitespace = " \t\r\n\"";
        const auto begin = s.find_first_not_of(whitespace);

        if (begin == std::string_view::npos)
        {
            return {};
        }

        const auto end = s.find_last_not_of(whitespace);
        return s.substr(begin, end - begin + 1);
    }

    /// Returns an ordered map of config key -> reference to the field.
    std::map<std::string, ConfigRef> GetFieldMap()
    {
        return {
            {"bDebugLogging", std::ref(debugLogging)},
            {"bUseAnpFileFormat", std::ref(useAnpFileFormat)},
            {"fCatmullRomSplineAlpha", std::ref(catmullRomSplineAlpha)},
            {"fFactionDangerCost", std::ref(factionDangerCost)},
            {"fRandomPathMaxDistance", std::ref(randomPathMaxDistance)},
            {"iBezierCurvePoints", std::ref(bezierCurvePoints)},
            {"iCatmullRomSplinePoints", std::ref(catmullRomSplinePoints)},
            {"iMaxPointPath", std::ref(maxPointPath)},
            {"iMaxPolyPath", std::ref(maxPolyPath)},
            {"iMaxSearchNodes", std::ref(maxSearchNodes)},
            {"iMmapFormat", std::ref(mmapFormat)},
            {"iPort", std::ref(port)},
            {"sCustomMmapPattern", std::ref(customMmapPattern)},
            {"sCustomMmtilePattern", std::ref(customMmtilePattern)},
            {"sIp", std::ref(ip)},
            {"sMmapsPath", std::ref(mmapsPath)},
            {"sPreloadMaps", std::ref(preloadMaps)},
        };
    }
};
