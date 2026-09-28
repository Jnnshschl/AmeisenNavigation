# AmeisenNavigation 🐜

[![CI](https://github.com/Jnnshschl/AmeisenNavigation/actions/workflows/ci.yml/badge.svg)](https://github.com/Jnnshschl/AmeisenNavigation/actions/workflows/ci.yml)

TCP navigation server and navmesh exporter for World of Warcraft bots (built for the AmeisenBotX), based on
[recastnavigation](https://github.com/recastnavigation/recastnavigation). Runs on **Windows and Linux**.

- **AmeisenNavigation.Server**: answers path, raycast, height and random point queries over a tiny binary TCP protocol.
  Serves TrinityCore/SkyFire **MMAPs** or its own **ANP** navmeshes.
- **AmeisenNavigation.Exporter**: builds ANP navmeshes straight from the game's MPQ archives, with area types for
  terrain, roads, cities, WMOs, doodads, water/ocean/lava/slime and Alliance/Horde territory.
- **AmeisenNavigation.Client** (C#): client library with auto reconnect, used by the bot and the WPF tester.

## Features 🚀

- Straight and randomized paths, path smoothing (Chaikin, Catmull-Rom, Bezier) with optional navmesh validation
- Move small deltas along the navmesh surface, movement raycasts (with hit position and wall normal)
- Ground height queries that pick the right floor (bridges, buildings, caves)
- Random points on the mesh / around a position
- Per client query filters: faction aware costs (avoid enemy territory), custom area costs, ghost mode
- Thread per connection, concurrent clients never block each other, maps loaded lazily or at startup (`sPreloadMaps`)

## Quick start 📝

1. Download the latest [release](https://github.com/Jnnshschl/AmeisenNavigation/releases) (or build it, see below).
2. Get navmeshes:
   - **ANP** (recommended): export them with the exporter (see [Exporter](#exporter)).
   - **MMAP**: generate them with the TrinityCore `mmaps_generator` (or SkyFire for 5.4.8).
3. Start the server once, it creates `config.cfg` next to the executable. Set `sMmapsPath` (and `bUseAnpFileFormat=1`
   for ANP navmeshes), then start it again. A different config file can be passed as first argument.

```
AmeisenNavigation.Server [config.cfg]
```

### Server configuration

| Key | Default | Description |
| --- | --- | --- |
| `sIp` / `iPort` | `127.0.0.1` / `47110` | Listen address and port |
| `sMmapsPath` | `C:\meshes\` (`./meshes/` on Linux) | Folder with the `.anp` or `.mmap`/`.mmtile` files |
| `bUseAnpFileFormat` | `0` | `1` = ANP navmeshes from the exporter, `0` = MMAPs |
| `iMmapFormat` | `0` | `0` auto detect, `1` TrinityCore 3.3.5a, `2` SkyFire 5.4.8, `-1` custom patterns |
| `sCustomMmapPattern` / `sCustomMmtilePattern` | TC 3.3.5a names | `std::format` patterns for `iMmapFormat=-1` (args: mapId, x, y) |
| `sPreloadMaps` | empty | Comma separated map ids loaded at startup, e.g. `0,1,530,571` |
| `iMaxPolyPath` | `2048` | Max polygons of a path corridor |
| `iMaxPointPath` | `512` | Max points of a returned path |
| `iMaxSearchNodes` | `65535` | A* node pool size per client and map |
| `fFactionDangerCost` | `3.0` | Cost multiplier for enemy faction areas (ANP) |
| `fRandomPathMaxDistance` | `1.0` | Max offset of randomized path corners |
| `iCatmullRomSplinePoints` / `fCatmullRomSplineAlpha` | `4` / `0.5` | Catmull-Rom samples per segment / parametrization |
| `iBezierCurvePoints` | `8` | Bezier samples per curve |
| `bDebugLogging` | `0` | Log every request (with timings) |

Invalid values are reported and replaced by defaults, `#`/`;` start comments. `CTRL+C` (or `SIGTERM`) shuts the
server down gracefully.

## Exporter

```
AmeisenNavigation.Exporter --wow <WoW folder> --output <mesh folder> [options]

  -m, --map <ids>      Only export these map ids (comma separated, e.g. 0,1,530,571)
  -t, --tile <x,y>     Only export a single ADT (debugging)
  -j, --threads <n>    Worker threads (default: all cores)
  -d, --debug          Debug logging + area debug images (<output>/debug/area_<map>_<x>_<y>.bmp)
  -l, --list-maps      List the maps in Map.dbc
```

The exporter reads the MPQs in the client's load order (custom `patch-X.MPQ` files override the base archives),
extracts terrain, liquids, WMOs and doodads, and builds one Detour tile per ADT:

- Triangles are bucketed per tile and sub-tile, so the build time grows linearly with the map size.
- WMO/doodad placements are extracted once (by unique id) and are never clipped at ADT borders.
- Tiles that would exceed Detour's 65535 vertex limit are automatically rebuilt with coarser settings.
- Area ids (see `AmeisenNavigation.Pack/src/AnpFormat.hpp`) come in {neutral, Alliance, Horde} triplets for ground,
  road, city, WMO, doodad, water, ocean, lava and slime.

## Protocol

Every message (both directions) is framed as `int32 size | uint8 type | payload[size - 1]` (little endian). Every
request gets exactly one response with the same type. Positions are WoW coordinates (`x`, `y`, `z` floats).

| Type | Request | Response |
| --- | --- | --- |
| `0` PATH | `int mapId, int flags, Vector3 start, Vector3 end` | `Vector3[]` (single zero vector on failure) |
| `1` MOVE_ALONG_SURFACE | `int mapId, Vector3 start, Vector3 end` | `Vector3` reachable position (zero on failure) |
| `2` RANDOM_POINT | `int mapId` | `Vector3` |
| `3` RANDOM_POINT_AROUND | `int mapId, Vector3 center, float radius` | `Vector3` |
| `4` CAST_RAY | `int mapId, Vector3 start, Vector3 end` | `Vector3`: end if clear, zero if blocked |
| `5` RANDOM_PATH | same as PATH | `Vector3[]` with randomized corners |
| `7` CONFIGURE_FILTER | `uint8 state, pad[3], int count, {uint8 area, pad[3], float cost}[count]` | `bool` |
| `8` GET_HEIGHT | `int mapId, Vector3 position` | `Vector3` on the mesh (zero on failure) |
| `9` GET_CONFIG | - | `int mmapFormat, int useAnp, int pathLength, char path[]` |
| `10` CAST_RAY_EX | `int mapId, Vector3 start, Vector3 end` | `int hit (1/0/-1 error), float t, Vector3 position, Vector3 normal` |

Path flags: `1` Chaikin, `2` Catmull-Rom, `4` Bezier (one smoothing), `8` validate with closest point on poly,
`16` validate with move along surface. Client states: `0` normal, `1` Alliance, `2` Horde, `3` dead.
Unknown message types get an empty response.

### C# client

```csharp
using var nav = new AmeisenNavClient("127.0.0.1", 47110);
nav.TryConnect();
nav.SetClientState(ClientState.NormalAlliance);
nav.ApplyFilter();

Vector3[]? path = nav.GetPath(0, start, end, PathFlags.SmoothCatmullRom | PathFlags.ValidateMoveAlongSurface);
RaycastHit hit = nav.CastRayEx(0, position, target);
```

## Building 🛠️

Requirements: a C++20 compiler (MSVC 2022+, GCC 13+, Clang 17+), CMake 3.21+. The .NET projects need the .NET 10 SDK.

```bash
# Linux / Windows (CMake)
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build -C Release        # unit + end-to-end tests
```

On Windows the Visual Studio solution (`AmeisenNavigation.sln`) works as well. CMake uses the prebuilt StormLib
from `dep/` with MSVC and fetches StormLib from GitHub everywhere else (`-DANAV_USE_BUNDLED_STORMLIB=OFF` forces that).

| CMake option | Default | |
| --- | --- | --- |
| `ANAV_BUILD_SERVER` / `ANAV_BUILD_EXPORTER` / `ANAV_BUILD_TESTS` | `ON` | Select targets |
| `ANAV_ENABLE_OPENMP` / `ANAV_ENABLE_LTO` | `ON` | Parallel loading/building, link time optimization |
| `ANAV_SANITIZE` | `OFF` | AddressSanitizer + UndefinedBehaviorSanitizer (GCC/Clang) |
| `ANAV_WARNINGS_AS_ERRORS` | `OFF` | `-Werror` / `/WX` for project code |

`anav_benchmark [adtsPerAxis] [queries]` builds a synthetic map at production resolution and measures build and
query throughput.

## Project layout

| Folder | |
| --- | --- |
| `AmeisenNavigation/` | Core library: nav sources (ANP, MMAP), query filters, path smoothing, per client state |
| `AmeisenNavigation.Pack/` | ANP file format (miniz), area ids and Detour helpers shared by server and exporter |
| `AmeisenNavigation.Server/` | Server executable: config, protocol, request handlers |
| `AnTCP.Server/` / `AnTCP.Client/` | Minimal cross-platform request/response TCP layer |
| `AmeisenNavigation.Exporter/` | MPQ/ADT/WMO/M2 parsing and the tile build pipeline |
| `AmeisenNavigation.Client/` | C# client library |
| `AmeisenNavigation.Tester/` | WPF tool to visualize maps, navmeshes and paths |
| `tests/` | Unit and end-to-end tests (synthetic world through the real exporter pipeline and TCP server) |

## Credits 🙌

- TrinityCore - [GitHub](https://github.com/TrinityCore/TrinityCore)
- recastnavigation - [GitHub](https://github.com/recastnavigation/recastnavigation)
- StormLib - [GitHub](https://github.com/ladislav-zezula/StormLib)
- miniz - [GitHub](https://github.com/richgel999/miniz)
