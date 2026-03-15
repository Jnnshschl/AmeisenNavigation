#include "Main.hpp"

int __cdecl main(int argc, const char* argv[])
{
    Logger::Initialize();

    std::cout << "\033[96m"
              << "      ___                   _                 _   __           " << std::endl
              << "     /   |  ____ ___  ___  (_)_______  ____  / | / /___ __   __" << std::endl
              << "    / /| | / __ `__ \\/ _ \\/ / ___/ _ \\/ __ \\/  |/ / __ `/ | / /" << std::endl
              << "   / ___ |/ / / / / /  __/ (__  )  __/ / / / /|  / /_/ /| |/ / " << std::endl
              << "  /_/  |_/_/ /_/ /_/\\___/_/____/\\___/_/ /_/_/ |_/\\__,_/ |___/  " << std::endl
              << "                                          Server " << AMEISENNAV_VERSION << "\033[0m" << std::endl
              << std::endl;

    std::filesystem::path configPath(std::filesystem::path(argv[0]).parent_path().string() + "\\config.cfg");
    auto* config = new AmeisenNavConfig();

    if (argc > 1)
    {
        configPath = std::filesystem::path(argv[1]);

        if (!std::filesystem::exists(configPath))
        {
            LogE("Configfile does not exists: \"", argv[1], "\"");
            std::cin.get();
            delete config;
            return 1;
        }
    }

    if (std::filesystem::exists(configPath))
    {
        config->Load(configPath);
        LogI("Loaded Configfile: \"", configPath.string(), "\"");

        // directly save again to add new entries to it
        config->Save(configPath);
    }
    else
    {
        config->Save(configPath);

        LogI("Created default Configfile: \"", configPath.string(), "\"");
        LogI("Edit it and restart the server, press any key to exit...");
        std::cin.get();
        delete config;
        return 1;
    }

    // validate config
    if (!std::filesystem::exists(config->mmapsPath))
    {
        LogE("MMAPS folder does not exists: \"", config->mmapsPath, "\"");
        std::cin.get();
        delete config;
        return 1;
    }

    if (config->maxPolyPath <= 0)
    {
        LogE("iMaxPolyPath has to be a value > 0");
        std::cin.get();
        delete config;
        return 1;
    }

    if (config->port <= 0 || config->port > 65535)
    {
        LogE("iPort has to be a value bewtween 1 and 65535");
        std::cin.get();
        delete config;
        return 1;
    }

    if (config->maxSearchNodes <= 0 || config->maxSearchNodes > 65535)
    {
        LogE("iMaxSearchNodes has to be a value bewtween 1 and 65535");
        std::cin.get();
        delete config;
        return 1;
    }

    // set ctrl+c handler to cleanup stuff when we exit
    if (!SetConsoleCtrlHandler(SigIntHandler, 1))
    {
        LogE("SetConsoleCtrlHandler() failed: ", GetLastError());
        std::cin.get();
        delete config;
        return 1;
    }

    // NavServer takes ownership of config
    g_NavServer = new NavServer(config);
    g_NavServer->RegisterCallbacks();

    LogS("Starting server on: ", config->ip, ":", std::to_string(config->port));
    g_NavServer->Run();

    LogI("Stopped server...");

    delete g_NavServer;
    g_NavServer = nullptr;
}

int __stdcall SigIntHandler(unsigned long signal)
{
    if (signal == CTRL_C_EVENT || signal == CTRL_CLOSE_EVENT)
    {
        LogI("Received CTRL-C or CTRL-EXIT, stopping server...");
        if (g_NavServer) g_NavServer->Stop();
    }

    return 1;
}

void OnClientConnect(ClientHandler* handler) noexcept
{
    LogI("Client Connected: ", handler->GetIpAddress(), ":", handler->GetPort());

    g_NavServer->AllocClientBuffers(handler->GetId());
    g_NavServer->Nav()->NewClient(handler->GetId(), static_cast<MmapFormat>(g_NavServer->Config()->mmapFormat));
}

void OnClientDisconnect(ClientHandler* handler) noexcept
{
    if (!g_NavServer->HasClientBuffers(handler->GetId()))
        return;

    g_NavServer->Nav()->FreeClient(handler->GetId());
    g_NavServer->FreeClientBuffers(handler->GetId());

    LogI("Client Disconnected: ", handler->GetIpAddress(), ":", handler->GetPort());
}

void PathCallback(ClientHandler* handler, char type, const void* data, int size) noexcept
{
    GenericPathCallback(handler, type, data, size, PathType::STRAIGHT);
}

void RandomPathCallback(ClientHandler* handler, char type, const void* data, int size) noexcept
{
    GenericPathCallback(handler, type, data, size, PathType::RANDOM);
}

void MoveAlongSurfaceCallback(ClientHandler* handler, char type, const void* data, int size) noexcept
{
    const MoveRequestData request = *reinterpret_cast<const MoveRequestData*>(data);
    Vector3 point;
    g_NavServer->Nav()->MoveAlongSurface(handler->GetId(), request.mapId, request.start, request.end, point);
    handler->SendData(type, point, sizeof(Vector3));
}

void CastRayCallback(ClientHandler* handler, char type, const void* data, int size) noexcept
{
    const CastRayData request = *reinterpret_cast<const CastRayData*>(data);
    dtRaycastHit hit;

    if (g_NavServer->Nav()->CastMovementRay(handler->GetId(), request.mapId, request.start, request.end, &hit))
    {
        handler->SendData(type, request.end, sizeof(Vector3));
    }
    else
    {
        Vector3 zero;
        handler->SendData(type, zero, sizeof(Vector3));
    }
}

void GenericPathCallback(ClientHandler* handler, char type, const void* data, int size, PathType pathType) noexcept
{
    const PathRequestData request = *reinterpret_cast<const PathRequestData*>(data);
    bool pathGenerated = false;

    auto buffers = g_NavServer->GetClientBuffers(handler->GetId());
    Path& path = *buffers.first;
    Path& pathMisc = *buffers.second;

    switch (pathType)
    {
        case PathType::STRAIGHT:
            pathGenerated = g_NavServer->Nav()->GetPath(handler->GetId(), request.mapId, request.start, request.end, path);
            break;
        case PathType::RANDOM:
            pathGenerated = g_NavServer->Nav()->GetRandomPath(handler->GetId(), request.mapId, request.start, request.end, path,
                                               g_NavServer->Config()->randomPathMaxDistance);
            break;
    }

    if (pathGenerated)
    {
        HandlePathFlagsAndSendData(handler, request.mapId, request.flags, path, pathMisc, type, pathType);
    }
    else
    {
        Vector3 zero;
        handler->SendData(type, zero, sizeof(Vector3));
    }

    path.pointCount = 0;
    pathMisc.pointCount = 0;
}

void RandomPointCallback(ClientHandler* handler, char type, const void* data, int size) noexcept
{
    const int mapId = *reinterpret_cast<const int*>(data);
    Vector3 point;
    g_NavServer->Nav()->GetRandomPoint(handler->GetId(), mapId, point);
    handler->SendData(type, point, sizeof(Vector3));
}

void RandomPointAroundCallback(ClientHandler* handler, char type, const void* data, int size) noexcept
{
    const RandomPointAroundData request = *reinterpret_cast<const RandomPointAroundData*>(data);
    Vector3 point;
    g_NavServer->Nav()->GetRandomPointAround(handler->GetId(), request.mapId, request.start, request.radius, point);
    handler->SendData(type, point, sizeof(Vector3));
}

void ExplorePolyCallback(ClientHandler* handler, char type, const void* data, int size) noexcept
{
    const ExplorePolyData request = *reinterpret_cast<const ExplorePolyData*>(data);

    auto buffers = g_NavServer->GetClientBuffers(handler->GetId());
    Path& path = *buffers.first;
    Path& pathMisc = *buffers.second;

    bool pathGenerated =
        g_NavServer->Nav()->GetPolyExplorationPath(handler->GetId(), request.mapId, &request.firstPolyPoint, request.polyPointCount,
                                    path, pathMisc, request.start, request.viewDistance);

    if (pathGenerated)
    {
        HandlePathFlagsAndSendData(handler, request.mapId, request.flags, path, pathMisc, type, PathType::STRAIGHT);
    }
    else
    {
        Vector3 zero;
        handler->SendData(type, zero, sizeof(Vector3));
    }

    path.pointCount = 0;
    pathMisc.pointCount = 0;
}

void ConfigureFilterCallback(ClientHandler* handler, char type, const void* data, int size) noexcept
{
    bool result = true;

    // IMPORTANT: Use a pointer into the original data buffer, NOT a local copy.
    // ConfigureFilterData uses a "flexible array" pattern where filterConfigs[1..N]
    // follow firstFilterConfig in the raw buffer. A local copy would only contain
    // the struct's 16 bytes, causing filterConfigs[1+] to read garbage from the stack.
    const auto* request = reinterpret_cast<const ConfigureFilterData*>(data);

    AmeisenNavClient* client = g_NavServer->Nav()->GetClient(handler->GetId());
    if (!client)
    {
        result = false;
        handler->SendData(type, &result, sizeof(bool));
        return;
    }

    // Validate entry count against received data size
    int expectedSize = static_cast<int>(sizeof(ConfigureFilterData))
        + (request->filterConfigCount - 1) * static_cast<int>(sizeof(FilterConfig));

    if (request->filterConfigCount <= 0 || size < expectedSize)
    {
        result = false;
        handler->SendData(type, &result, sizeof(bool));
        return;
    }

    client->ResetQueryFilter();

    const FilterConfig* filterConfigs = &request->firstFilterConfig;

    for (int i = 0; i < request->filterConfigCount; ++i)
    {
        client->ConfigureQueryFilter(filterConfigs[i].areaId, filterConfigs[i].cost);
    }

    client->UpdateQueryFilter(request->state);
    handler->SendData(type, &result, sizeof(bool));
}

void NavServer::RegisterCallbacks() noexcept
{
    server_->SetOnClientConnected(OnClientConnect);
    server_->SetOnClientDisconnected(OnClientDisconnect);

    server_->AddCallback(static_cast<char>(MessageType::PATH), PathCallback);
    server_->AddCallback(static_cast<char>(MessageType::RANDOM_POINT_AROUND), RandomPointAroundCallback);
    server_->AddCallback(static_cast<char>(MessageType::MOVE_ALONG_SURFACE), MoveAlongSurfaceCallback);
    server_->AddCallback(static_cast<char>(MessageType::CAST_RAY), CastRayCallback);
    server_->AddCallback(static_cast<char>(MessageType::RANDOM_PATH), RandomPathCallback);
    server_->AddCallback(static_cast<char>(MessageType::RANDOM_POINT), RandomPointCallback);
    server_->AddCallback(static_cast<char>(MessageType::EXPLORE_POLY), ExplorePolyCallback);
    server_->AddCallback(static_cast<char>(MessageType::CONFIGURE_FILTER), ConfigureFilterCallback);
}
