#pragma once

#include "AnTcpServer.hpp"

#include "../../AmeisenNavigation/src/AmeisenNavigation.hpp"

#include "Config/Config.hpp"
#include "Logging/AmeisenLogger.hpp"

#include <filesystem>
#include <iostream>
#include <mutex>

constexpr auto AMEISENNAV_VERSION = "1.8.3.2";
constexpr auto VEC3_SIZE = sizeof(float) * 3;

enum class MessageType
{
    PATH,                   // Generate a simple straight path
    MOVE_ALONG_SURFACE,     // Move an entity by small deltas using pathfinding (usefull to prevent falling off edges...)
    RANDOM_POINT,           // Get a random point on the mesh
    RANDOM_POINT_AROUND,    // Get a random point on the mesh in a circle
    CAST_RAY,               // Cast a movement ray to test for obstacles
    RANDOM_PATH,            // Generate a straight path where the nodes get offsetted by a random value
    EXPLORE_POLY,           // Generate a route to explore the polygon (W.I.P)
};

enum class PathType
{
    STRAIGHT,
    RANDOM,
};

enum class PathRequestFlags : int
{
    NONE = 0,
    SMOOTH_CHAIKIN = 1,
    SMOOTH_CATMULLROM = 2,
    SMOOTH_BEZIERCURVE = 4,
};

struct PathRequestData
{
    int mapId;
    int flags;
    float start[3];
    float end[3];
};

struct MoveRequestData
{
    int mapId;
    float start[3];
    float end[3];
};

struct CastRayData
{
    int mapId;
    float start[3];
    float end[3];
};

struct RandomPointAroundData
{
    int mapId;
    float start[3];
    float radius;
};

struct ExplorePolyData
{
    int mapId;
    int flags;
    float start[3];
    float viewDistance;
    int polyPointCount;
    float firstPolyPoint[3];
};

inline AnTcpServer* Server = nullptr;
inline AmeisenNavigation* Nav = nullptr;
inline AmeisenNavConfig* Config = nullptr;

inline std::unordered_map<size_t, std::pair<float*, float*>> ClientPathBuffers;

int __stdcall SigIntHandler(unsigned long signal);

void OnClientConnect(ClientHandler* handler) noexcept;
void OnClientDisconnect(ClientHandler* handler) noexcept;

void PathCallback(ClientHandler* handler, char type, const void* data, int size) noexcept;
void RandomPathCallback(ClientHandler* handler, char type, const void* data, int size) noexcept;
void MoveAlongSurfaceCallback(ClientHandler* handler, char type, const void* data, int size) noexcept;
void CastRayCallback(ClientHandler* handler, char type, const void* data, int size) noexcept;
void GenericPathCallback(ClientHandler* handler, char type, const void* data, int size, PathType pathType) noexcept;

void RandomPointCallback(ClientHandler* handler, char type, const void* data, int size) noexcept;
void RandomPointAroundCallback(ClientHandler* handler, char type, const void* data, int size) noexcept;

void ExplorePolyCallback(ClientHandler* handler, char type, const void* data, int size) noexcept;

inline void HandlePathFlagsAndSendData(ClientHandler* handler, int flags, int pathSize, float* pathBuffer, char type) noexcept
{
    int smoothedPathSize = 0;
    float* smoothedPathBuffer = ClientPathBuffers[handler->GetId()].second;

    if ((flags & static_cast<int>(PathRequestFlags::SMOOTH_CATMULLROM)) && pathSize >= 12)
    {
        Nav->SmoothPathCatmullRom(pathBuffer, pathSize, smoothedPathBuffer, &smoothedPathSize, Config->catmullRomSplinePoints, Config->catmullRomSplineAlpha);
        handler->SendData(type, smoothedPathBuffer, smoothedPathSize * sizeof(float));
    }
    else if ((flags & static_cast<int>(PathRequestFlags::SMOOTH_CHAIKIN)) && pathSize >= 9)
    {
        Nav->SmoothPathChaikinCurve(pathBuffer, pathSize, smoothedPathBuffer, &smoothedPathSize);
        handler->SendData(type, smoothedPathBuffer, smoothedPathSize * sizeof(float));
    }
    else if ((flags & static_cast<int>(PathRequestFlags::SMOOTH_BEZIERCURVE)) && pathSize >= 12)
    {
        Nav->SmoothPathBezier(pathBuffer, pathSize, smoothedPathBuffer, &smoothedPathSize, Config->bezierCurvePoints);
        handler->SendData(type, smoothedPathBuffer, smoothedPathSize * sizeof(float));
    }
    else
    {
        handler->SendData(type, pathBuffer, pathSize * sizeof(float));
    }
}
