#include <atomic>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include "NavServer.hpp"
#include "Utils/Logger.hpp"

#ifdef _WIN32
#include <io.h>
#else
#include <csignal>
#include <unistd.h>
#endif

namespace {
/// Target for the signal handler. Stop() only sets an atomic flag, so calling it from a signal is safe.
std::atomic<NavServer*> g_SignalTarget{nullptr};

#ifdef _WIN32
BOOL WINAPI ConsoleCtrlHandler(DWORD signal)
{
    if (signal == CTRL_C_EVENT || signal == CTRL_BREAK_EVENT || signal == CTRL_CLOSE_EVENT)
    {
        if (NavServer* server = g_SignalTarget.load())
        {
            server->Stop();
        }

        return TRUE;
    }

    return FALSE;
}

bool InstallSignalHandlers() { return SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE) != 0; }

bool IsInteractive() { return _isatty(_fileno(stdin)) != 0; }
#else
void SignalHandler(int)
{
    if (NavServer* server = g_SignalTarget.load())
    {
        server->Stop();
    }
}

bool InstallSignalHandlers()
{
    struct sigaction action{};
    action.sa_handler = SignalHandler;
    sigemptyset(&action.sa_mask);

    std::signal(SIGPIPE, SIG_IGN);
    return sigaction(SIGINT, &action, nullptr) == 0 && sigaction(SIGTERM, &action, nullptr) == 0;
}

bool IsInteractive() { return false; }
#endif

/// Keep the console window open when started by double click on Windows, never block services.
void WaitForKeyIfInteractive()
{
    if (IsInteractive())
    {
        LogI("Press enter to exit...");
        std::cin.get();
    }
}

void PrintBanner()
{
    const bool color = Logger::IsColorEnabled();
    std::fputs(std::format("{}"
                           "      ___                   _                 _   __           \n"
                           "     /   |  ____ ___  ___  (_)_______  ____  / | / /___ __   __\n"
                           "    / /| | / __ `__ \\/ _ \\/ / ___/ _ \\/ __ \\/  |/ / __ `/ | / /\n"
                           "   / ___ |/ / / / / /  __/ (__  )  __/ / / / /|  / /_/ /| |/ / \n"
                           "  /_/  |_/_/ /_/ /_/\\___/_/____/\\___/_/ /_/_/ |_/\\__,_/ |___/  \n"
                           "                                          Server {}{}\n\n",
                           color ? "\033[96m" : "", AMEISENNAV_VERSION, color ? "\033[0m" : "")
                   .c_str(),
               stdout);
}

void PrintUsage(const char* exe)
{
    std::printf("Usage: %s [config.cfg]\n\n"
                "  config.cfg   Path to the config file (default: config.cfg next to the executable).\n"
                "               A default config is created if the file does not exist.\n"
                "  --help       Show this help.\n"
                "  --version    Print the version.\n\n"
                "Every config key can be overridden with an environment variable named ANAV_<key>,\n"
                "e.g. ANAV_sMmapsPath=/meshes ANAV_iPort=47110 ANAV_bUseAnpFileFormat=1.\n",
                exe);
}

std::filesystem::path DefaultConfigPath(const char* argv0)
{
    std::error_code ec;
    const auto exe = std::filesystem::absolute(argv0 ? argv0 : "", ec);
    return (ec ? std::filesystem::current_path(ec) : exe.parent_path()) / "config.cfg";
}
} // namespace

int main(int argc, const char* argv[])
{
    Logger::Initialize();

    std::filesystem::path configPath = DefaultConfigPath(argc > 0 ? argv[0] : nullptr);
    bool configFromArgs = false;

    for (int i = 1; i < argc; ++i)
    {
        const std::string_view arg = argv[i];

        if (arg == "--help" || arg == "-h" || arg == "/?")
        {
            PrintUsage(argv[0]);
            return 0;
        }

        if (arg == "--version" || arg == "-v")
        {
            std::printf("AmeisenNavigation.Server %s (AnTCP %s)\n", AMEISENNAV_VERSION, ANTCP_SERVER_VERSION);
            return 0;
        }

        configPath = std::filesystem::path(arg);
        configFromArgs = true;
    }

    PrintBanner();

    AmeisenNavConfig config;
    const bool configExists = std::filesystem::exists(configPath);

    if (configExists)
    {
        std::vector<std::string> parseErrors;
        config.Load(configPath, &parseErrors);

        for (const auto& error : parseErrors)
        {
            LogW("Config: ", error);
        }

        LogI("Loaded config: \"", configPath.string(), "\"");

        // Save again so new options show up in existing config files (environment overrides aren't saved).
        config.Save(configPath);
    }

    std::vector<std::string> envErrors;
    const int envOverrides = config.ApplyEnvironment("ANAV_", &envErrors);

    for (const auto& error : envErrors)
    {
        LogW("Config: ", error);
    }

    if (envOverrides > 0)
    {
        LogI("Config: ", envOverrides, " value(s) from ANAV_* environment variables");
    }

    // Without a config file the environment alone can configure the server (containers).
    if (!configExists && envOverrides == 0)
    {
        if (configFromArgs)
        {
            LogE("Config file does not exist: \"", configPath.string(), "\"");
            WaitForKeyIfInteractive();
            return 1;
        }

        if (!config.Save(configPath))
        {
            LogE("Failed to create default config: \"", configPath.string(), "\"");
            WaitForKeyIfInteractive();
            return 1;
        }

        LogI("Created default config: \"", configPath.string(), "\"");
        LogI("Edit it and restart the server.");
        WaitForKeyIfInteractive();
        return 1;
    }

    Logger::SetDebugEnabled(config.debugLogging || Logger::IsDebugEnabled());

    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    NavServer::ValidateConfig(config, errors, warnings);

    for (const auto& warning : warnings)
    {
        LogW("Config: ", warning);
    }

    if (!errors.empty())
    {
        for (const auto& error : errors)
        {
            LogE("Config: ", error);
        }

        WaitForKeyIfInteractive();
        return 1;
    }

    NavServer server(config);
    g_SignalTarget.store(&server);

    if (!InstallSignalHandlers())
    {
        LogW("Failed to install signal handlers, CTRL+C will not shut down gracefully");
    }

    LogI("Config: format=", config.useAnpFileFormat ? "ANP" : "MMAP", " maxPolyPath=", config.maxPolyPath,
         " maxPointPath=", config.maxPointPath, " maxSearchNodes=", config.maxSearchNodes);
    LogI("Config: meshes=\"", config.mmapsPath, "\"");

    server.PreloadMaps();

    LogS("Starting server on ", config.ip, ":", config.port);
    const AnTcpError result = server.Run();
    g_SignalTarget.store(nullptr);

    if (result != AnTcpError::Success)
    {
        LogE("Server failed to start (AnTcpError ", static_cast<int>(result), "), is the port already in use?");
        WaitForKeyIfInteractive();
        return 1;
    }

    LogI("Server shutdown complete.");
    return 0;
}
