#pragma once

#include <cstddef>
#include <cstdint>

#include "Clients/ClientState.hpp"
#include "Utils/Vector3.hpp"

/// Wire-protocol enums and structs shared between the C++ server and the C# client.
/// These must stay in sync with AmeisenNavigation.Client/WireFormat.cs (little endian, natural alignment).
///
/// Every request gets exactly one response with the same message type:
///   PATH / RANDOM_PATH      -> Vector3[] (a single zero vector on failure)
///   MOVE_ALONG_SURFACE      -> Vector3   (zero on failure)
///   RANDOM_POINT(_AROUND)   -> Vector3   (zero on failure)
///   CAST_RAY                -> Vector3   (end position if the ray is clear, zero if blocked/failed)
///   CONFIGURE_FILTER        -> bool
///   GET_HEIGHT              -> Vector3   (zero on failure)
///   GET_CONFIG              -> GetConfigResponseHeader + meshes path bytes
///   CAST_RAY_EX             -> CastRayExResponse
///   unknown types           -> empty payload

enum class MessageType : unsigned char
{
    PATH,                // Generate a simple straight path
    MOVE_ALONG_SURFACE,  // Move an entity by small deltas using pathfinding
    RANDOM_POINT,        // Get a random point on the mesh
    RANDOM_POINT_AROUND, // Get a random point on the mesh in a circle
    CAST_RAY,            // Cast a movement ray to test for obstacles
    RANDOM_PATH,         // Generate a straight path with random offsets
    EXPLORE_POLY,        // Reserved (not implemented)
    CONFIGURE_FILTER,    // Configure the client's dtQueryFilter area costs
    GET_HEIGHT,          // Get the navmesh terrain height at a position
    GET_CONFIG,          // Get the server's configuration (meshes path, format, etc.)
    CAST_RAY_EX,         // Cast a movement ray and get hit position, normal and fraction
};

enum class PathType
{
    STRAIGHT, // Request a simple straight path
    RANDOM,   // Request a path with small random deltas per position
};

enum class PathRequestFlags : int
{
    NONE = 0,
    SMOOTH_CHAIKIN = 1 << 0,     // Smooth path using Chaikin Curve
    SMOOTH_CATMULLROM = 1 << 1,  // Smooth path using Catmull-Rom Spline
    SMOOTH_BEZIERCURVE = 1 << 2, // Smooth path using Bezier Curve
    VALIDATE_CPOP = 1 << 3,      // Validate smoothed path using closestPointOnPoly
    VALIDATE_MAS = 1 << 4,       // Validate smoothed path using moveAlongSurface
};

constexpr bool HasFlag(int flags, PathRequestFlags flag) noexcept { return (flags & static_cast<int>(flag)) != 0; }

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

struct CastRayExResponse
{
    int hit;          // 1 = wall hit, 0 = clear, -1 = error (no mesh / start off-mesh)
    float t;          // fraction of start->end travelled before the hit (1 if clear)
    Vector3 position; // hit position (end if clear)
    Vector3 normal;   // wall normal (zero if clear)
};

struct RandomPointAroundData
{
    int mapId;
    Vector3 start;
    float radius;
};

struct GetHeightData
{
    int mapId;
    Vector3 position;
};

struct GetConfigResponseHeader
{
    int mmapFormat;
    int useAnpFileFormat;
    int pathLength;
};

struct FilterConfig
{
    unsigned char areaId;
    float cost;
};

/// Variable length: header followed by filterConfigCount FilterConfig entries.
struct ConfigureFilterHeader
{
    ClientState state;
    int filterConfigCount;
};

// Layout checks, the C# client relies on these exact sizes/offsets.
static_assert(sizeof(PathRequestData) == 32);
static_assert(sizeof(MoveRequestData) == 28);
static_assert(sizeof(CastRayData) == 28);
static_assert(sizeof(CastRayExResponse) == 32);
static_assert(sizeof(RandomPointAroundData) == 20);
static_assert(sizeof(GetHeightData) == 16);
static_assert(sizeof(GetConfigResponseHeader) == 12);
static_assert(sizeof(FilterConfig) == 8 && offsetof(FilterConfig, cost) == 4);
static_assert(sizeof(ConfigureFilterHeader) == 8 && offsetof(ConfigureFilterHeader, filterConfigCount) == 4);

/// Maximum number of filter entries in one CONFIGURE_FILTER request (one per Detour area).
constexpr int MAX_FILTER_CONFIGS = 64;
