#pragma once

#include "AnTcpServer.hpp"

#include "../../AmeisenNavigation/src/AmeisenNavigation.hpp"

#include "Config/Config.hpp"
#include "Logging/AmeisenLogger.hpp"

#include <filesystem>
#include <iostream>
#include <mutex>

constexpr auto AMEISENNAV_VERSION = "1.8.4.0";

enum class MessageType
{
    PATH,                   // Generate a simple straight path
    MOVE_ALONG_SURFACE,     // Move an entity by small deltas using pathfinding (usefull to prevent falling off edges...)
    RANDOM_POINT,           // Get a random point on the mesh
    RANDOM_POINT_AROUND,    // Get a random point on the mesh in a circle
    CAST_RAY,               // Cast a movement ray to test for obstacles
    RANDOM_PATH,            // Generate a straight path where the nodes get offsetted by a random value
    EXPLORE_POLY,           // Generate a route to explore the polygon (W.I.P)
    CONFIGURE_FILTER,       // Cpnfigure the clients dtQueryFilter area costs
};

enum class PathType
{
    STRAIGHT,   // Request a simple straight path
    RANDOM,     // Request a path where every position will be move by a small random delta
};

enum class PathRequestFlags : int
{
    NONE = 0,
    SMOOTH_CHAIKIN = 1 << 0,        // Smooth path using Chaikin Curve
    SMOOTH_CATMULLROM = 1 << 1,     // Smooth path using Catmull-Rom Spline
    SMOOTH_BEZIERCURVE = 1 << 2,    // Smooth path using Bezier Curve
    VALIDATE_CPOP = 1 << 3,         // Validate smoothed path using closestPointOnPoly
    VALIDATE_MAS = 1 << 4,          // Validate smoothed path using moveAlongSurface
};

struct PathRequestData
{
    int mapId;
    int flags;
    Vector3 start;
    Vector3 end;
};

struct MoveRequestData
{
    int mapId;
    Vector3 start;
    Vector3 end;
};

struct CastRayData
{
    int mapId;
    Vector3 start;
    Vector3 end;
};

struct RandomPointAroundData
{
    int mapId;
    Vector3 start;
    float radius;
};

struct ExplorePolyData
{
    int mapId;
    int flags;
    Vector3 start;
    float viewDistance;
    int polyPointCount;
    Vector3 firstPolyPoint;
};

struct FilterConfig
{
    char areaId;
    float cost;
};

struct ConfigureFilterData
{
    ClientState state;
    int filterConfigCount;
    FilterConfig firstFilterConfig;
};

inline AnTcpServer* Server = nullptr;
inline AmeisenNavigation* Nav = nullptr;
inline AmeisenNavConfig* Config = nullptr;

inline std::unordered_map<size_t, std::pair<Path*, Path*>> ClientPathBuffers;

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

void ConfigureFilterCallback(ClientHandler* handler, char type, const void* data, int size) noexcept;

inline void HandlePathFlagsAndSendData(ClientHandler* handler, int mapId, int flags, Path& path, Path& smoothPath, char type, PathType pathType) noexcept
{
    Path* pathToSend = &path;
    Path* altPath = &smoothPath;
    bool shouldValidate = false;

    if ((flags & static_cast<int>(PathRequestFlags::SMOOTH_CHAIKIN)) && path.pointCount >= 3)
    {
        Nav->SmoothPathChaikinCurve(path, smoothPath);
        pathToSend = &smoothPath;
        altPath = &path;
        shouldValidate = true;
    }
    else if ((flags & static_cast<int>(PathRequestFlags::SMOOTH_CATMULLROM)) && path.pointCount >= 4)
    {
        Nav->SmoothPathCatmullRom(path, smoothPath, Config->catmullRomSplinePoints, Config->catmullRomSplineAlpha);
        pathToSend = &smoothPath;
        altPath = &path;
        shouldValidate = true;
    }
    else if ((flags & static_cast<int>(PathRequestFlags::SMOOTH_BEZIERCURVE)) && path.pointCount >= 4)
    {
        Nav->SmoothPathBezier(path, smoothPath, Config->bezierCurvePoints);
        pathToSend = &smoothPath;
        altPath = &path;
        shouldValidate = true;
    }

    if (pathType == PathType::RANDOM)
    {
        shouldValidate = true;
    }

    // validate random paths
    if (shouldValidate && pathToSend->pointCount > 0)
    {
        if ((flags & static_cast<int>(PathRequestFlags::VALIDATE_CPOP)))
        {
            path.pointCount = 0;
            Nav->PostProcessClosestPointOnPoly(handler->GetId(), mapId, *pathToSend, *altPath);
            pathToSend = altPath;
        }
        else if ((flags & static_cast<int>(PathRequestFlags::VALIDATE_MAS)))
        {
            path.pointCount = 0;
            Nav->PostProcessMoveAlongSurface(handler->GetId(), mapId, *pathToSend, *altPath);
            pathToSend = altPath;
        }
    }

    handler->SendData(type, pathToSend->points, pathToSend->pointCount * sizeof(Vector3));
}
