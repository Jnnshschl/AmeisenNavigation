#pragma once

#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <sstream>
#include <string>
#include <variant>

/// Type-safe configuration entry: wraps a reference to the actual config field.
using ConfigRef = std::variant<
    std::reference_wrapper<bool>,
    std::reference_wrapper<int>,
    std::reference_wrapper<float>,
    std::reference_wrapper<std::string>
>;

struct AmeisenNavConfig
{
    // ── Configuration Fields ─────────────────────────────────────────

    bool useAnpFileFormat = false;
    float catmullRomSplineAlpha = 0.5f;
    float factionDangerCost = 3.0f;
    float randomPathMaxDistance = 1.0f;
    int bezierCurvePoints = 8;
    int catmullRomSplinePoints = 4;
    int maxPointPath = 512;
    int maxPolyPath = 2048;
    int maxSearchNodes = 65535;
    int mmapFormat = 0; // MmapFormat::UNKNOWN
    int port = 47110;
    std::string ip = "127.0.0.1";
    std::string mmapsPath = "C:\\meshes\\";

    // ── Serialization ────────────────────────────────────────────────

    void Save(const std::filesystem::path& path)
    {
        std::ofstream out(path);
        if (!out.is_open())
            return;

        for (const auto& [key, ref] : GetFieldMap())
        {
            std::visit([&](auto&& r) {
                using T = std::decay_t<decltype(r.get())>;
                if constexpr (std::is_same_v<T, bool>)
                    out << key << '=' << static_cast<unsigned int>(r.get()) << '\n';
                else if constexpr (std::is_same_v<T, float>)
                    out << key << '=' << std::to_string(r.get()) << '\n';
                else
                    out << key << '=' << r.get() << '\n';
            }, ref);
        }
    }

    void Load(const std::filesystem::path& path)
    {
        std::ifstream in(path);
        if (!in.is_open())
            return;

        auto fields = GetFieldMap();

        for (std::string line; std::getline(in, line);)
        {
            auto delim = line.find('=');
            if (delim == std::string::npos)
                continue;

            std::string key = line.substr(0, delim);
            std::string val = line.substr(delim + 1);

            auto it = fields.find(key);
            if (it == fields.end())
                continue;

            std::visit([&](auto&& r) {
                using T = std::decay_t<decltype(r.get())>;
                try
                {
                    if constexpr (std::is_same_v<T, bool>)
                        r.get() = std::stoi(val) > 0;
                    else if constexpr (std::is_same_v<T, int>)
                        r.get() = std::stoi(val);
                    else if constexpr (std::is_same_v<T, float>)
                        r.get() = std::stof(val);
                    else if constexpr (std::is_same_v<T, std::string>)
                        r.get() = val;
                }
                catch (...) { /* skip malformed values */ }
            }, it->second);
        }
    }

private:
    /// Returns an ordered map of config key -> reference to the field.
    /// Key prefix convention: b=bool, i=int, f=float, s=string.
    std::map<std::string, ConfigRef> GetFieldMap()
    {
        return {
            {"bUseAnpFileFormat",       std::ref(useAnpFileFormat)},
            {"fCatmullRomSplineAlpha",  std::ref(catmullRomSplineAlpha)},
            {"fFactionDangerCost",      std::ref(factionDangerCost)},
            {"fRandomPathMaxDistance",   std::ref(randomPathMaxDistance)},
            {"iBezierCurvePoints",      std::ref(bezierCurvePoints)},
            {"iCatmullRomSplinePoints", std::ref(catmullRomSplinePoints)},
            {"iMaxPointPath",           std::ref(maxPointPath)},
            {"iMaxPolyPath",            std::ref(maxPolyPath)},
            {"iMaxSearchNodes",         std::ref(maxSearchNodes)},
            {"iMmapFormat",             std::ref(mmapFormat)},
            {"iPort",                   std::ref(port)},
            {"sIp",                     std::ref(ip)},
            {"sMmapsPath",              std::ref(mmapsPath)},
        };
    }
};
