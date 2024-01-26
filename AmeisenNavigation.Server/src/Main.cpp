#include "Main.hpp"

int __cdecl main(int argc, const char* argv[])
{
#if defined(WIN32) || defined(WIN64)
    SetConsoleTitle(L"AmeisenNavigation Server");
    SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), 15);
#endif

    std::cout << "      ___                   _                 _   __           " << std::endl
        << "     /   |  ____ ___  ___  (_)_______  ____  / | / /___ __   __" << std::endl
        << "    / /| | / __ `__ \\/ _ \\/ / ___/ _ \\/ __ \\/  |/ / __ `/ | / /" << std::endl
        << "   / ___ |/ / / / / /  __/ (__  )  __/ / / / /|  / /_/ /| |/ / " << std::endl
        << "  /_/  |_/_/ /_/ /_/\\___/_/____/\\___/_/ /_/_/ |_/\\__,_/ |___/  " << std::endl
        << "                                          Server " << AMEISENNAV_VERSION << std::endl << std::endl;

    std::filesystem::path configPath(std::filesystem::path(argv[0]).parent_path().string() + "\\config.cfg");
    Config = new AmeisenNavConfig();

    if (argc > 1)
    {
        configPath = std::filesystem::path(argv[1]);

        if (!std::filesystem::exists(configPath))
        {
            LogE("Configfile does not exists: \"", argv[1], "\"");
            std::cin.get();
            return 1;
        }
    }

    if (std::filesystem::exists(configPath))
    {
        Config->Load(configPath);
        LogI("Loaded Configfile: \"", configPath.string(), "\"");

        // directly save again to add new entries to it
        Config->Save(configPath);
    }
    else
    {
        Config->Save(configPath);

        LogI("Created default Configfile: \"", configPath.string(), "\"");
        LogI("Edit it and restart the server, press any key to exit...");
        std::cin.get();
        return 1;
    }

    // validate config
    if (!std::filesystem::exists(Config->mmapsPath))
    {
        LogE("MMAPS folder does not exists: \"", Config->mmapsPath, "\"");
        std::cin.get();
        return 1;
    }

    if (Config->maxPolyPath <= 0)
    {
        LogE("iMaxPolyPath has to be a value > 0");
        std::cin.get();
        return 1;
    }

    if (Config->port <= 0 || Config->port > 65535)
    {
        LogE("iPort has to be a value bewtween 1 and 65535");
        std::cin.get();
        return 1;
    }

    if (Config->maxSearchNodes <= 0 || Config->maxSearchNodes > 65535)
    {
        LogE("iMaxSearchNodes has to be a value bewtween 1 and 65535");
        std::cin.get();
        return 1;
    }

    // set ctrl+c handler to cleanup stuff when we exit
    if (!SetConsoleCtrlHandler(SigIntHandler, 1))
    {
        LogE("SetConsoleCtrlHandler() failed: ", GetLastError());
        std::cin.get();
        return 1;
    }

    Nav = new AmeisenNavigation(Config->mmapsPath, Config->maxPolyPath, Config->maxSearchNodes);
    Server = new AnTcpServer(Config->ip, Config->port);

    Server->SetOnClientConnected(OnClientConnect);
    Server->SetOnClientDisconnected(OnClientDisconnect);

    Server->AddCallback(static_cast<char>(MessageType::PATH), PathCallback);
    Server->AddCallback(static_cast<char>(MessageType::RANDOM_POINT_AROUND), RandomPointAroundCallback);
    Server->AddCallback(static_cast<char>(MessageType::MOVE_ALONG_SURFACE), MoveAlongSurfaceCallback);
    Server->AddCallback(static_cast<char>(MessageType::CAST_RAY), CastRayCallback);

    Server->AddCallback(static_cast<char>(MessageType::RANDOM_PATH), RandomPathCallback);
    Server->AddCallback(static_cast<char>(MessageType::RANDOM_POINT), RandomPointCallback);

    Server->AddCallback(static_cast<char>(MessageType::EXPLORE_POLY), ExplorePolyCallback);

    LogS("Starting server on: ", Config->ip, ":", std::to_string(Config->port));
    Server->Run();

    LogI("Stopped server...");
    delete Config;
    delete Nav;
    delete Server;

    for (const auto& [id, buffers] : ClientPathBuffers)
    {
        if (buffers.first)
        {
            delete[] buffers.first->points;
            delete[] buffers.first;
        }

        if (buffers.second)
        {
            delete[] buffers.second->points;
            delete[] buffers.second;
        }
    }
}

int __stdcall SigIntHandler(unsigned long signal)
{
    if (signal == CTRL_C_EVENT || signal == CTRL_CLOSE_EVENT)
    {
        LogI("Received CTRL-C or CTRL-EXIT, stopping server...");
        Server->Stop();
    }

    return 1;
}

void OnClientConnect(ClientHandler* handler) noexcept
{
    LogI("Client Connected: ", handler->GetIpAddress(), ":", handler->GetPort());

    ClientPathBuffers[handler->GetId()] = std::make_pair
    (
        new Path(Config->maxPointPath),
        new Path(Config->maxPointPath)
    );

    Nav->NewClient(handler->GetId(), static_cast<MmapFormat>(Config->mmapFormat));
}

void OnClientDisconnect(ClientHandler* handler) noexcept
{
    Nav->FreeClient(handler->GetId());

    delete[] ClientPathBuffers[handler->GetId()].first->points;
    delete ClientPathBuffers[handler->GetId()].first;
    ClientPathBuffers[handler->GetId()].first = nullptr;

    delete[] ClientPathBuffers[handler->GetId()].second->points;
    delete ClientPathBuffers[handler->GetId()].second;
    ClientPathBuffers[handler->GetId()].second = nullptr;

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
    Nav->MoveAlongSurface(handler->GetId(), request.mapId, request.start, request.end, point);
    handler->SendData(type, point, sizeof(Vector3));
}

void CastRayCallback(ClientHandler* handler, char type, const void* data, int size) noexcept
{
    const CastRayData request = *reinterpret_cast<const CastRayData*>(data);
    dtRaycastHit hit;

    if (Nav->CastMovementRay(handler->GetId(), request.mapId, request.start, request.end, &hit))
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

    Path& path = *ClientPathBuffers[handler->GetId()].first;
    Path& pathMisc = *ClientPathBuffers[handler->GetId()].second;

    switch (pathType)
    {
    case PathType::STRAIGHT:
        pathGenerated = Nav->GetPath(handler->GetId(), request.mapId, request.start, request.end, path);
        break;
    case PathType::RANDOM:
        pathGenerated = Nav->GetRandomPath(handler->GetId(), request.mapId, request.start, request.end, path, Config->randomPathMaxDistance);
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
    Nav->GetRandomPoint(handler->GetId(), mapId, point);
    handler->SendData(type, point, sizeof(Vector3));
}

void RandomPointAroundCallback(ClientHandler* handler, char type, const void* data, int size) noexcept
{
    const RandomPointAroundData request = *reinterpret_cast<const RandomPointAroundData*>(data);
    Vector3 point;
    Nav->GetRandomPointAround(handler->GetId(), request.mapId, request.start, request.radius, point);
    handler->SendData(type, point, sizeof(Vector3));
}

void ExplorePolyCallback(ClientHandler* handler, char type, const void* data, int size) noexcept
{
    const ExplorePolyData request = *reinterpret_cast<const ExplorePolyData*>(data);

    Path& path = *ClientPathBuffers[handler->GetId()].first;
    Path& pathMisc = *ClientPathBuffers[handler->GetId()].second;

    bool pathGenerated = Nav->GetPolyExplorationPath
    (
        handler->GetId(),
        request.mapId,
        &request.firstPolyPoint,
        request.polyPointCount,
        path,
        pathMisc,
        request.start,
        request.viewDistance
    );

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
