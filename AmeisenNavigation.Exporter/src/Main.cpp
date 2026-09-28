#include <charconv>
#include <cstdio>
#include <string_view>

#ifdef _OPENMP
#include <omp.h>
#endif

#include "../../AmeisenNavigation/src/Utils/Logger.hpp"
#include "Exporter.hpp"

namespace {
struct CommandLine
{
    ExportOptions options;
    int threads = 0;
    bool listMaps = false;
};

void PrintUsage()
{
    std::printf(
        "Usage: AmeisenNavigation.Exporter --wow <path> --output <path> [options]\n\n"
        "  -w, --wow <path>        WoW client folder (or its Data folder)\n"
        "  -o, --output <path>     Output folder for the .anp files\n"
        "  -m, --map <ids>         Only export these map ids (comma separated, e.g. 0,1,530,571)\n"
        "  -t, --tile <x,y>        Only export a single ADT (debugging)\n"
        "  -j, --threads <n>       Number of worker threads (default: all cores)\n"
        "  -s, --skip-existing     Don't rebuild maps whose .anp file already exists\n"
        "  -d, --debug             Debug logging and area debug images (<output>/debug)\n"
        "  -l, --list-maps         List the maps in Map.dbc and exit\n"
        "  -h, --help              Show this help\n\n"
        "Example: AmeisenNavigation.Exporter -w \"C:\\WoW\" -o \"C:\\meshes\" -m 0,1 -j 8\n");
}

bool ParseInt(std::string_view s, int& out) noexcept
{
    const auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), out);
    return ec == std::errc() && ptr == s.data() + s.size();
}

/// Returns false (after printing the reason) if the arguments are invalid.
bool ParseArguments(int argc, char** argv, CommandLine& cmd, bool& exitEarly)
{
    exitEarly = false;
    auto& options = cmd.options;

    for (int i = 1; i < argc; ++i)
    {
        const std::string_view arg = argv[i];
        const bool hasValue = i + 1 < argc;

        if (arg == "--help" || arg == "-h")
        {
            PrintUsage();
            exitEarly = true;
            return true;
        }

        if (arg == "--debug" || arg == "-d")
        {
            options.debug = true;
        }
        else if (arg == "--list-maps" || arg == "-l")
        {
            cmd.listMaps = true;
        }
        else if (arg == "--skip-existing" || arg == "-s")
        {
            options.skipExisting = true;
        }
        else if ((arg == "--wow" || arg == "-w") && hasValue)
        {
            options.wowDir = argv[++i];
        }
        else if ((arg == "--output" || arg == "-o") && hasValue)
        {
            options.outputDir = argv[++i];
        }
        else if ((arg == "--map" || arg == "-m") && hasValue)
        {
            std::string_view list = argv[++i];

            while (!list.empty())
            {
                const auto comma = list.find(',');
                const auto token = list.substr(0, comma);
                int id = 0;

                if (!ParseInt(token, id) || id < 0)
                {
                    LogE("Invalid --map value: ", token);
                    return false;
                }

                options.mapIds.push_back(id);
                list = comma == std::string_view::npos ? std::string_view() : list.substr(comma + 1);
            }
        }
        else if ((arg == "--tile" || arg == "-t") && hasValue)
        {
            const std::string_view tile = argv[++i];
            const auto comma = tile.find(',');

            if (comma == std::string_view::npos || !ParseInt(tile.substr(0, comma), options.tileX)
                || !ParseInt(tile.substr(comma + 1), options.tileY) || options.tileX < 0 || options.tileX >= 64
                || options.tileY < 0 || options.tileY >= 64)
            {
                LogE("Invalid --tile value: ", tile, " (expected x,y with 0 <= x,y < 64)");
                return false;
            }
        }
        else if ((arg == "--threads" || arg == "-j") && hasValue)
        {
            if (!ParseInt(argv[++i], cmd.threads) || cmd.threads < 1)
            {
                LogE("Invalid --threads value: ", argv[i]);
                return false;
            }
        }
        else
        {
            LogE("Unknown or incomplete argument: ", arg);
            PrintUsage();
            return false;
        }
    }

    if (options.wowDir.empty() || (options.outputDir.empty() && !cmd.listMaps))
    {
        LogE("Missing required arguments.");
        PrintUsage();
        return false;
    }

    return true;
}

void PrintBanner()
{
    const bool color = Logger::IsColorEnabled();
    std::fputs(std::format("{}"
                           "      ___                   _                 _   __\n"
                           "     /   |  ____ ___  ___  (_)_______  ____  / | / /___ __   __\n"
                           "    / /| | / __ `__ \\/ _ \\/ / ___/ _ \\/ __ \\/  |/ / __ `/ | / /\n"
                           "   / ___ |/ / / / / /  __/ (__  )  __/ / / / /|  / /_/ /| |/ / \n"
                           "  /_/  |_/_/ /_/ /_/\\___/_/____/\\___/_/ /_/_/ |_/\\__,_/ |___/\n"
                           "                                          Exporter {}{}\n\n",
                           color ? "\033[96m" : "", AMEISENNAV_VERSION, color ? "\033[0m" : "")
                   .c_str(),
               stdout);
}
} // namespace

int main(int argc, char** argv)
{
    Logger::Initialize();

    CommandLine cmd;
    bool exitEarly = false;

    if (!ParseArguments(argc, argv, cmd, exitEarly))
    {
        return 1;
    }

    if (exitEarly)
    {
        return 0;
    }

    Logger::SetDebugEnabled(cmd.options.debug || Logger::IsDebugEnabled());
    PrintBanner();

#ifdef _OPENMP
    if (cmd.threads > 0)
    {
        omp_set_num_threads(cmd.threads);
    }

    // Tiles use nested parallelism for single-ADT exports (MSVC only implements OpenMP 2.0).
#if _OPENMP >= 200805
    omp_set_max_active_levels(2);
#else
    omp_set_nested(1);
#endif
#endif

    if (cmd.listMaps)
    {
        const auto maps = ListMaps(cmd.options.wowDir);

        for (const auto& [id, name] : maps)
        {
            std::printf("%5u  %s\n", id, name.c_str());
        }

        return maps.empty() ? 1 : 0;
    }

    const ExportReport report = RunExport(cmd.options);

    if (!report.setupOk)
    {
        return 1;
    }

    return report.failedMaps > 0 ? 2 : 0;
}
