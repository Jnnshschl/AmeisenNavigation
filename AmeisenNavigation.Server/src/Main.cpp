#include "Main.hpp"

int main(int argc, const char* argv[])
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

    if (Config->maxPointPath <= 0)
    {
        LogE("iMaxPointPath has to be a value > 0");
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
        LogE("iMaxPolyPath has to be a value bewtween 1 and 65535");
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

    Nav = new AmeisenNavigation(Config->mmapsPath, Config->maxPolyPath, Config->maxPointPath);
    Server = new AnTcpServer(Config->ip, Config->port);

    Server->SetOnClientConnected(OnClientConnect);
    Server->SetOnClientDisconnected(OnClientDisconnect);

    Server->AddCallback(static_cast<char>(MessageType::PATH), PathCallback);
    Server->AddCallback(static_cast<char>(MessageType::RANDOM_POINT), RandomPointCallback);
    Server->AddCallback(static_cast<char>(MessageType::RANDOM_POINT_AROUND), RandomPointAroundCallback);
    Server->AddCallback(static_cast<char>(MessageType::MOVE_ALONG_SURFACE), MoveAlongSurfaceCallback);
    Server->AddCallback(static_cast<char>(MessageType::CAST_RAY), CastRayCallback);

    LogS("Starting server on: ", Config->ip, ":", std::to_string(Config->port));
    Server->Run();

    LogI("Stopped server...");
    delete Config;
    delete Nav;
    delete Server;

    for (const auto& kv : ClientPathBuffers)
    {
        if (kv.second.second)
        {
            delete[] kv.second.first;
        }

        if (kv.second.second)
        {
            delete[] kv.second.first;
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

void OnClientConnect(ClientHandler* handler)
{
    LogI("Client Connected: ", handler->GetIpAddress(), ":", handler->GetPort());

    ClientPathBuffers[handler->GetId()] = std::make_pair(new float[Config->maxPointPath * 3], new float[Config->maxPointPath * 3]);
    Nav->NewClient(handler->GetId());
}

void OnClientDisconnect(ClientHandler* handler)
{
    Nav->FreeClient(handler->GetId());

    delete[] ClientPathBuffers[handler->GetId()].first;
    ClientPathBuffers[handler->GetId()].first = nullptr;

    delete[] ClientPathBuffers[handler->GetId()].second;
    ClientPathBuffers[handler->GetId()].second = nullptr;

    LogI("Client Disconnected: ", handler->GetIpAddress(), ":", handler->GetPort());
}

void PathCallback(ClientHandler* handler, char type, const void* data, int size)
{
    const PathRequestData request = *reinterpret_cast<const PathRequestData*>(data);

    int pathSize = 0;
    float* pathBuffer = ClientPathBuffers[handler->GetId()].first;

    if (Nav->GetPath(handler->GetId(), request.mapId, request.start, request.end, pathBuffer, &pathSize))
    {
        if ((request.flags & static_cast<int>(PathRequestFlags::CATMULLROM)) && pathSize > 9)
        {
            int smoothedPathSize = 0;
            float* smoothedPathBuffer = ClientPathBuffers[handler->GetId()].second;
            Nav->SmoothPathCatmullRom(pathBuffer, pathSize, smoothedPathBuffer, &smoothedPathSize, Config->catmullRomSplinePoints, Config->catmullRomSplineAlpha);

            handler->SendData(type, smoothedPathBuffer, smoothedPathSize * sizeof(float));
        }
        else if ((request.flags & static_cast<int>(PathRequestFlags::CHAIKIN)) && pathSize > 6)
        {
            int smoothedPathSize = 0;
            float* smoothedPathBuffer = ClientPathBuffers[handler->GetId()].second;
            Nav->SmoothPathChaikinCurve(pathBuffer, pathSize, smoothedPathBuffer, &smoothedPathSize);

            handler->SendData(type, smoothedPathBuffer, smoothedPathSize * sizeof(float));
        }
        else
        {
            handler->SendData(type, pathBuffer, pathSize * sizeof(float));
        }
    }
    else
    {
        float zero[3]{};
        handler->SendData(type, zero, VEC3_SIZE);
    }
}

void RandomPointCallback(ClientHandler* handler, char type, const void* data, int size)
{
    const int mapId = *reinterpret_cast<const int*>(data);
    float point[3]{};

    Nav->GetRandomPoint(handler->GetId(), mapId, point);
    handler->SendData(type, point, VEC3_SIZE);
}

void RandomPointAroundCallback(ClientHandler* handler, char type, const void* data, int size)
{
    const RandomPointAroundData request = *reinterpret_cast<const RandomPointAroundData*>(data);
    float point[3]{};

    Nav->GetRandomPointAround(handler->GetId(), request.mapId, request.start, request.radius, point);
    handler->SendData(type, point, VEC3_SIZE);
}

void MoveAlongSurfaceCallback(ClientHandler* handler, char type, const void* data, int size)
{
    const MoveRequestData request = *reinterpret_cast<const MoveRequestData*>(data);
    float point[3]{};

    Nav->MoveAlongSurface(handler->GetId(), request.mapId, request.start, request.end, point);
    handler->SendData(type, point, VEC3_SIZE);
}

void CastRayCallback(ClientHandler* handler, char type, const void* data, int size)
{
    const CastRayData request = *reinterpret_cast<const CastRayData*>(data);
    dtRaycastHit hit;

    if (Nav->CastMovementRay(handler->GetId(), request.mapId, request.start, request.end, &hit))
    {
        handler->SendData(type, request.end, VEC3_SIZE);
    }
    else
    {
        float zero[3]{};
        handler->SendData(type, zero, VEC3_SIZE);
    }
}