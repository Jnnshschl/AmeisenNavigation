#pragma once

#include "AnTcpServer.hpp"

#include "../../AmeisenNavigation/src/AmeisenNavigation.hpp"

#include "Config/Config.hpp"
#include "Logging/AmeisenLogger.hpp"

#include <filesystem>
#include <iostream>
#include <mutex>

constexpr auto AMEISENNAV_VERSION = "1.7.5.0";

constexpr auto VEC3_SIZE = sizeof(float) * 3;

enum class MessageType
{
    PATH,
    MOVE_ALONG_SURFACE,
    RANDOM_POINT,
    RANDOM_POINT_AROUND,
    CAST_RAY,
    RANDOM_PATH,
};

enum class PathType
{
    STRAIGHT,
    RANDOM,
};

enum class PathRequestFlags : int
{
    NONE = 0,
    CHAIKIN = 1,
    CATMULLROM = 2,
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

inline AnTcpServer* Server = nullptr;
inline AmeisenNavigation* Nav = nullptr;
inline AmeisenNavConfig* Config = nullptr;

inline std::unordered_map<int, std::pair<float*, float*>> ClientPathBuffers;

int __stdcall SigIntHandler(unsigned long signal);

void OnClientConnect(ClientHandler* handler) noexcept;
void OnClientDisconnect(ClientHandler* handler) noexcept;

void PathCallback(ClientHandler* handler, char type, const void* data, int size) noexcept;
void RandomPathCallback(ClientHandler* handler, char type, const void* data, int size) noexcept;
void RandomPointCallback(ClientHandler* handler, char type, const void* data, int size) noexcept;
void RandomPointAroundCallback(ClientHandler* handler, char type, const void* data, int size) noexcept;
void MoveAlongSurfaceCallback(ClientHandler* handler, char type, const void* data, int size) noexcept;
void CastRayCallback(ClientHandler* handler, char type, const void* data, int size) noexcept;

void GenericPathCallback(ClientHandler* handler, char type, const void* data, int size, PathType pathType) noexcept;
