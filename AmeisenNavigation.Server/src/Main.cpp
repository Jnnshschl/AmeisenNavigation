#include "Main.hpp"

int __cdecl main(int argc, const char* argv[])
{
    Logger::Initialize();

    fputs(std::format(
        "\033[96m"
        "      ___                   _                 _   __           \n"
        "     /   |  ____ ___  ___  (_)_______  ____  / | / /___ __   __\n"
        "    / /| | / __ `__ \\/ _ \\/ / ___/ _ \\/ __ \\/  |/ / __ `/ | / /\n"
        "   / ___ |/ / / / / /  __/ (__  )  __/ / / / /|  / /_/ /| |/ / \n"
        "  /_/  |_/_/ /_/ /_/\\___/_/____/\\___/_/ /_/_/ |_/\\__,_/ |___/  \n"
        "                                          Server {}\033[0m\n\n",
        AMEISENNAV_VERSION).c_str(), stdout);

    std::filesystem::path configPath(std::filesystem::path(argv[0]).parent_path().string() + "\\config.cfg");
    auto config = std::make_unique<AmeisenNavConfig>();

    if (argc > 1)
    {
        configPath = std::filesystem::path(argv[1]);

        if (!std::filesystem::exists(configPath))
        {
            LogE("Configfile does not exist: \"", argv[1], "\"");
            std::cin.get();
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
        return 1;
    }

    // validate config
    if (!std::filesystem::exists(config->mmapsPath))
    {
        LogE("MMAPS folder does not exist: \"", config->mmapsPath, "\"");
        std::cin.get();
        return 1;
    }

    if (config->maxPolyPath <= 0)
    {
        LogE("iMaxPolyPath has to be a value > 0");
        std::cin.get();
        return 1;
    }

    if (config->port <= 0 || config->port > 65535)
    {
        LogE("iPort has to be a value between 1 and 65535");
        std::cin.get();
        return 1;
    }

    if (config->maxSearchNodes <= 0 || config->maxSearchNodes > 65535)
    {
        LogE("iMaxSearchNodes has to be a value between 1 and 65535");
        std::cin.get();
        return 1;
    }

    if (config->maxPointPath <= 0)
    {
        LogE("iMaxPointPath has to be a value > 0");
        std::cin.get();
        return 1;
    }

    if (config->bezierCurvePoints < 2)
    {
        LogW("iBezierCurvePoints too low, clamping to 2");
        config->bezierCurvePoints = 2;
    }

    if (config->catmullRomSplinePoints < 2)
    {
        LogW("iCatmullRomSplinePoints too low, clamping to 2");
        config->catmullRomSplinePoints = 2;
    }

    if (config->factionDangerCost < 0.0f)
    {
        LogW("fFactionDangerCost negative, clamping to 0.0");
        config->factionDangerCost = 0.0f;
    }

    // set ctrl+c handler to cleanup stuff when we exit
    if (!SetConsoleCtrlHandler(SigIntHandler, 1))
    {
        LogE("SetConsoleCtrlHandler() failed: ", GetLastError());
        std::cin.get();
        return 1;
    }

    // NavServer takes ownership of config
    auto* configPtr = config.get();
    g_NavServer = std::make_unique<NavServer>(std::move(config));
    g_NavServer->RegisterCallbacks();

    LogI("Config: maxPolyPath=", configPtr->maxPolyPath,
         " maxPointPath=", configPtr->maxPointPath,
         " maxSearchNodes=", configPtr->maxSearchNodes,
         " format=", configPtr->useAnpFileFormat ? "ANP" : "MMAP");
    LogI("Config: meshes=\"", configPtr->mmapsPath, "\"");
    LogS("Starting server on: ", configPtr->ip, ":", std::to_string(configPtr->port));
    g_NavServer->Run();

    LogI("Server shutdown complete.");
    g_NavServer.reset();
}

int __stdcall SigIntHandler(unsigned long signal)
{
    if (signal == CTRL_C_EVENT || signal == CTRL_CLOSE_EVENT)
    {
        LogI("Received CTRL-C or CTRL-EXIT, stopping server...");
        if (g_NavServer) { g_NavServer->Stop(); }
    }

    return 1;
}

void OnClientConnect(ClientHandler* handler)
{
    LogI("Client Connected: ", handler->GetIpAddress(), ":", handler->GetPort());

    g_NavServer->AllocClientBuffers(handler->GetId());
    g_NavServer->Nav()->NewClient(handler->GetId(), static_cast<MmapFormat>(g_NavServer->Config()->mmapFormat));
}

void OnClientDisconnect(ClientHandler* handler)
{
    if (!g_NavServer->HasClientBuffers(handler->GetId()))
        return;

    g_NavServer->Nav()->FreeClient(handler->GetId());
    g_NavServer->FreeClientBuffers(handler->GetId());

    LogI("Client Disconnected: ", handler->GetIpAddress(), ":", handler->GetPort());
}

void PathCallback(ClientHandler* handler, AnTcpMessageType type, const void* data, int size)
{
    GenericPathCallback(handler, type, data, size, PathType::STRAIGHT);
}

void RandomPathCallback(ClientHandler* handler, AnTcpMessageType type, const void* data, int size)
{
    GenericPathCallback(handler, type, data, size, PathType::RANDOM);
}

void MoveAlongSurfaceCallback(ClientHandler* handler, AnTcpMessageType type, const void* data, int size)
{
    if (size < static_cast<int>(sizeof(MoveRequestData)))
    {
        LogE("MoveAlongSurface: packet too small (", size, " < ", sizeof(MoveRequestData), ")");
        return;
    }

    const MoveRequestData request = *reinterpret_cast<const MoveRequestData*>(data);
    Vector3 point;
    bool ok = g_NavServer->Nav()->MoveAlongSurface(handler->GetId(), request.mapId, request.start, request.end, point);
    LogD("[", handler->GetId(), "] MoveAlongSurface map=", request.mapId, ok ? " ok" : " FAIL");
    handler->SendData(type, point, sizeof(Vector3));
}

void CastRayCallback(ClientHandler* handler, AnTcpMessageType type, const void* data, int size)
{
    if (size < static_cast<int>(sizeof(CastRayData)))
    {
        LogE("CastRay: packet too small (", size, " < ", sizeof(CastRayData), ")");
        return;
    }

    const CastRayData request = *reinterpret_cast<const CastRayData*>(data);
    dtRaycastHit hit;

    bool rayHit = g_NavServer->Nav()->CastMovementRay(handler->GetId(), request.mapId, request.start, request.end, &hit);
    LogD("[", handler->GetId(), "] CastRay map=", request.mapId, rayHit ? " hit" : " miss");

    if (rayHit)
    {
        handler->SendData(type, request.end, sizeof(Vector3));
    }
    else
    {
        Vector3 zero;
        handler->SendData(type, zero, sizeof(Vector3));
    }
}

void GenericPathCallback(ClientHandler* handler, AnTcpMessageType type, const void* data, int size, PathType pathType)
{
    if (size < static_cast<int>(sizeof(PathRequestData)))
    {
        LogE("PathRequest: packet too small (", size, " < ", sizeof(PathRequestData), ")");
        return;
    }

#ifdef _DEBUG
    const auto reqStart = std::chrono::high_resolution_clock::now();
#endif

    const PathRequestData request = *reinterpret_cast<const PathRequestData*>(data);
    bool pathGenerated = false;

    auto buffers = g_NavServer->GetClientBuffers(handler->GetId());
    if (!buffers.first || !buffers.second)
    {
        LogE("PathRequest: no buffers for client ", handler->GetId());
        return;
    }

    Path& path = *buffers.first;
    Path& pathMisc = *buffers.second;

    // Reset point counts so GetSpace() returns the full buffer size
    path.pointCount = 0;
    pathMisc.pointCount = 0;

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

#ifdef _DEBUG
    {
        const auto us = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::high_resolution_clock::now() - reqStart).count();
        LogD("[", handler->GetId(), "] ", (pathType == PathType::RANDOM ? "RandomPath" : "Path"),
             " map=", request.mapId, pathGenerated ? " ok" : " FAIL",
             " pts=", path.pointCount, " ", us, "us");
    }
#endif

    path.pointCount = 0;
    pathMisc.pointCount = 0;
}

void RandomPointCallback(ClientHandler* handler, AnTcpMessageType type, const void* data, int size)
{
    if (size < static_cast<int>(sizeof(int)))
    {
        LogE("RandomPoint: packet too small (", size, " < ", sizeof(int), ")");
        return;
    }

    const int mapId = *reinterpret_cast<const int*>(data);
    Vector3 point;
    bool ok = g_NavServer->Nav()->GetRandomPoint(handler->GetId(), mapId, point);
    LogD("[", handler->GetId(), "] RandomPoint map=", mapId, ok ? " ok" : " FAIL");
    handler->SendData(type, point, sizeof(Vector3));
}

void RandomPointAroundCallback(ClientHandler* handler, AnTcpMessageType type, const void* data, int size)
{
    if (size < static_cast<int>(sizeof(RandomPointAroundData)))
    {
        LogE("RandomPointAround: packet too small (", size, " < ", sizeof(RandomPointAroundData), ")");
        return;
    }

    const RandomPointAroundData request = *reinterpret_cast<const RandomPointAroundData*>(data);
    Vector3 point;
    bool ok = g_NavServer->Nav()->GetRandomPointAround(handler->GetId(), request.mapId, request.start, request.radius, point);
    LogD("[", handler->GetId(), "] RandomPointAround map=", request.mapId, " r=", request.radius, ok ? " ok" : " FAIL");
    handler->SendData(type, point, sizeof(Vector3));
}

void ConfigureFilterCallback(ClientHandler* handler, AnTcpMessageType type, const void* data, int size)
{
    bool result = true;

    if (size < static_cast<int>(sizeof(ConfigureFilterData)))
    {
        LogE("ConfigureFilter: packet too small (", size, " < ", sizeof(ConfigureFilterData), ")");
        result = false;
        handler->SendData(type, &result, sizeof(bool));
        return;
    }

    // Use a pointer into the original data buffer (flexible array pattern).
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
    LogD("[", handler->GetId(), "] ConfigureFilter entries=", request->filterConfigCount);
    handler->SendData(type, &result, sizeof(bool));
}

void GetHeightCallback(ClientHandler* handler, AnTcpMessageType type, const void* data, int size)
{
    if (size < static_cast<int>(sizeof(GetHeightData)))
    {
        LogE("GetHeight: packet too small (", size, " < ", sizeof(GetHeightData), ")");
        return;
    }

    const GetHeightData request = *reinterpret_cast<const GetHeightData*>(data);
    Vector3 point;
    bool ok = g_NavServer->Nav()->GetHeight(handler->GetId(), request.mapId, request.position, point);
    LogD("[", handler->GetId(), "] GetHeight map=", request.mapId, ok ? " ok" : " FAIL");
    handler->SendData(type, point, sizeof(Vector3));
}

void GetConfigCallback(ClientHandler* handler, AnTcpMessageType type, const void* data, int size)
{
    const auto* cfg = g_NavServer->Config();
    const auto& path = cfg->mmapsPath;

    GetConfigResponseHeader header;
    header.mmapFormat = cfg->mmapFormat;
    header.useAnpFileFormat = cfg->useAnpFileFormat ? 1 : 0;
    header.pathLength = static_cast<int>(path.size());

    // Build response: header + path string bytes
    const size_t totalSize = sizeof(GetConfigResponseHeader) + path.size();
    std::vector<char> buffer(totalSize);
    std::memcpy(buffer.data(), &header, sizeof(GetConfigResponseHeader));
    std::memcpy(buffer.data() + sizeof(GetConfigResponseHeader), path.data(), path.size());

    handler->SendData(type, buffer.data(), totalSize);
    LogD("[", handler->GetId(), "] GetConfig path=\"", path, "\"");
}

void NavServer::RegisterCallbacks()
{
    server_->SetOnClientConnected(OnClientConnect);
    server_->SetOnClientDisconnected(OnClientDisconnect);

    server_->AddCallback(static_cast<AnTcpMessageType>(MessageType::PATH), PathCallback);
    server_->AddCallback(static_cast<AnTcpMessageType>(MessageType::RANDOM_POINT_AROUND), RandomPointAroundCallback);
    server_->AddCallback(static_cast<AnTcpMessageType>(MessageType::MOVE_ALONG_SURFACE), MoveAlongSurfaceCallback);
    server_->AddCallback(static_cast<AnTcpMessageType>(MessageType::CAST_RAY), CastRayCallback);
    server_->AddCallback(static_cast<AnTcpMessageType>(MessageType::RANDOM_PATH), RandomPathCallback);
    server_->AddCallback(static_cast<AnTcpMessageType>(MessageType::RANDOM_POINT), RandomPointCallback);
    server_->AddCallback(static_cast<AnTcpMessageType>(MessageType::CONFIGURE_FILTER), ConfigureFilterCallback);
    server_->AddCallback(static_cast<AnTcpMessageType>(MessageType::GET_HEIGHT), GetHeightCallback);
    server_->AddCallback(static_cast<AnTcpMessageType>(MessageType::GET_CONFIG), GetConfigCallback);
}
