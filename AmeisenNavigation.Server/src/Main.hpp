#pragma once

#include "NavServer.hpp"
#include <Utils/Logger.hpp>

#include <filesystem>
#include <iostream>

constexpr auto AMEISENNAV_VERSION = "1.8.4.0";

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

inline void HandlePathFlagsAndSendData(ClientHandler* handler, int mapId, int flags, Path& path, Path& smoothPath,
                                       char type, PathType pathType) noexcept
{
    Path* pathToSend = &path;
    Path* altPath = &smoothPath;
    bool shouldValidate = false;

    auto* nav = g_NavServer->Nav();
    auto* cfg = g_NavServer->Config();

    if ((flags & static_cast<int>(PathRequestFlags::SMOOTH_CHAIKIN)) && path.pointCount >= 3)
    {
        nav->SmoothPathChaikinCurve(path, smoothPath);
        pathToSend = &smoothPath;
        altPath = &path;
        shouldValidate = true;
    }
    else if ((flags & static_cast<int>(PathRequestFlags::SMOOTH_CATMULLROM)) && path.pointCount >= 4)
    {
        nav->SmoothPathCatmullRom(path, smoothPath, cfg->catmullRomSplinePoints, cfg->catmullRomSplineAlpha);
        pathToSend = &smoothPath;
        altPath = &path;
        shouldValidate = true;
    }
    else if ((flags & static_cast<int>(PathRequestFlags::SMOOTH_BEZIERCURVE)) && path.pointCount >= 4)
    {
        nav->SmoothPathBezier(path, smoothPath, cfg->bezierCurvePoints);
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
            nav->PostProcessClosestPointOnPoly(handler->GetId(), mapId, *pathToSend, *altPath);
            pathToSend = altPath;
        }
        else if ((flags & static_cast<int>(PathRequestFlags::VALIDATE_MAS)))
        {
            path.pointCount = 0;
            nav->PostProcessMoveAlongSurface(handler->GetId(), mapId, *pathToSend, *altPath);
            pathToSend = altPath;
        }
    }

    handler->SendData(type, pathToSend->points, pathToSend->pointCount * sizeof(Vector3));
}
