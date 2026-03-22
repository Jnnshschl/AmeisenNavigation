#pragma once

#include "Clients/AmeisenNavClient.hpp"
#include "Utils/Vector3.hpp"

/// Wire-protocol enums and structs shared between the C++ server and C# client.
/// These must stay in sync with AmeisenNavigation.Client/WireFormat.cs.

enum class MessageType
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
    char areaId;
    float cost;
};

struct ConfigureFilterData
{
    ClientState state;
    int filterConfigCount;
    FilterConfig firstFilterConfig;
};
