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
- Area exploration: a short route that covers a polygon (grinding/gathering areas), see `EXPLORE_POLY`
- Partial path detection: optionally fail instead of walking to the closest point of an unreachable target
- Per client query filters: faction aware costs (avoid enemy territory), custom area costs, ghost mode
- Thread per connection, concurrent clients never block each other, maps loaded lazily or at startup (`sPreloadMaps`)
- Docker image and a hardened systemd unit, configuration through a file and/or `ANAV_<key>` environment variables

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

Or with Docker (see [Deployment](#deployment-)):

```bash
docker build -t ameisennav .
docker run -d -p 47110:47110 -v /srv/meshes:/meshes:ro ameisennav
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
| `fWaterCost` / `fBadLiquidCost` | `1.6` / `4.0` | Default costs of water/ocean and lava/slime (ANP and MMAP) |
| `fRoadCost` | `0.75` | Default cost of roads (ANP, < 1 prefers roads) |
| `fRandomPathMaxDistance` | `1.0` | Max offset of randomized path corners |
| `iCatmullRomSplinePoints` / `fCatmullRomSplineAlpha` | `4` / `0.5` | Catmull-Rom samples per segment / parametrization |
| `iBezierCurvePoints` | `8` | Bezier samples per curve |
| `iMaxClients` | `1024` | Simultaneous connections (one thread each), further ones are refused, `0` = unlimited |
| `iClientIdleTimeoutSec` | `0` | Disconnect clients that send nothing for this long, `0` = never (dead peers are detected by TCP keepalive after ~2 min anyway) |
| `iStatsIntervalSec` | `300` | Log request counts, rates and latencies per request type this often, `0` = off |
| `bDebugLogging` | `0` | Log every request (with timings) |

Invalid values are reported and replaced by defaults, `#`/`;` start comments. Every key can be overridden by an
environment variable named `ANAV_<key>` (e.g. `ANAV_sMmapsPath=/meshes`, `ANAV_iPort=47111`), the server then also
runs without a config file. `CTRL+C` (or `SIGTERM`) shuts the server down gracefully.

## Exporter

```
AmeisenNavigation.Exporter --wow <WoW folder> --output <mesh folder> [options]

  -m, --map <ids>      Only export these map ids (comma separated, e.g. 0,1,530,571)
  -t, --tile <x,y>     Only export a single ADT (debugging)
  -j, --threads <n>    Worker threads (default: all cores)
  -s, --skip-existing  Don't rebuild maps whose .anp file already exists (resume an interrupted export)
  -d, --debug          Debug logging + area debug images (<output>/debug/area_<map>_<x>_<y>.bmp)
  -l, --list-maps      List the maps in Map.dbc
```

The exit code is `0` on success, `1` if the client data couldn't be read and `2` if maps failed to build.

The exporter reads the MPQs in the client's load order (custom `patch-X.MPQ` files override the base archives),
extracts terrain, liquids, WMOs and doodads, and builds one Detour tile per ADT:

- Triangles are bucketed per tile and sub-tile, so the build time grows linearly with the map size.
- WMO/doodad placements are extracted once (by unique id) and are never clipped at ADT borders.
- WMO-only maps (most dungeons and raids) are built from the WDT's global WMO.
- Liquids come from MH2O (WotLK, sloped surfaces included) with an MCLQ fallback for older data.
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
| `6` EXPLORE_POLY | `int mapId, int flags, Vector3 start, float spacing, int count, Vector3 polygon[count]` | `Vector3[]` route (single zero vector on failure) |
| `7` CONFIGURE_FILTER | `uint8 state, pad[3], int count, {uint8 area, pad[3], float cost}[count]` | `bool` |
| `8` GET_HEIGHT | `int mapId, Vector3 position` | `Vector3` on the mesh (zero on failure) |
| `9` GET_CONFIG | - | `int mmapFormat, int useAnp, int pathLength, char path[]`, then `int protocolVersion, int maxPointPath, int versionLength, char version[]` |
| `10` CAST_RAY_EX | `int mapId, Vector3 start, Vector3 end` | `int hit (1/0/-1 error), float t, Vector3 position, Vector3 normal` |

Path flags: `1` Chaikin, `2` Catmull-Rom, `4` Bezier (one smoothing), `8` validate with closest point on poly,
`16` validate with move along surface, `32` require a complete path (fail if the end is unreachable instead of
returning a path to the closest reachable point). Client states: `0` normal, `1` Alliance, `2` Horde, `3` dead.
Unknown message types get an empty response. The protocol version (GET_CONFIG, `2` since 1.9) tells clients whether
EXPLORE_POLY and flag `32` are available, older clients simply ignore the GET_CONFIG trailer.

**EXPLORE_POLY** places waypoints on a hexagonal grid (`spacing` apart, e.g. twice the sight or gather range) inside
the polygon (x/y outline, max 256 points, its z selects the floor on stacked geometry), snaps them to the navmesh,
orders them into a short tour from `start` (nearest neighbour + 2-opt) and connects them with navmesh paths.
Unreachable waypoints are skipped, the spacing grows if more than 1024 waypoints would be needed and the route is cut
at `iMaxPointPath` points.

### C# client

```csharp
using var nav = new AmeisenNavClient("127.0.0.1", 47110);
nav.TryConnect();
nav.SetClientState(ClientState.NormalAlliance);
nav.ApplyFilter();

Vector3[]? path = nav.GetPath(0, start, end, PathFlags.SmoothCatmullRom | PathFlags.ValidateMoveAlongSurface);
Vector3[]? complete = nav.GetPath(0, start, end, PathFlags.RequireComplete); // null if unreachable
RaycastHit hit = nav.CastRayEx(0, position, target);
Vector3[]? route = nav.ExplorePolygon(0, position, grindArea, spacing: 40f);
ServerConfig? config = nav.GetConfig(); // ProtocolVersion, MaxPointPath, ServerVersion, ...
```

## Deployment 🐳

**Docker**: one image for the server and the exporter (`Dockerfile`, `deploy/docker-compose.yml`). The server
listens on `0.0.0.0:47110` and serves ANP navmeshes from `/meshes`; mount a config file at
`/etc/ameisennav/config.cfg` and/or set `ANAV_<key>` variables.

```bash
docker build -t ameisennav .
docker run -d --name ameisennav -p 47110:47110 -v /srv/meshes:/meshes:ro ameisennav
docker run -d -p 47110:47110 -v /srv/mmaps:/meshes:ro -e ANAV_bUseAnpFileFormat=0 ameisennav   # MMAPs

# Export navmeshes with the same image
docker run --rm -v /srv/wow:/wow:ro -v /srv/meshes:/meshes ameisennav exporter -w /wow -o /meshes -m 0,1 -s
```

Tagged releases (`v*`) also push the image to `ghcr.io/jnnshschl/ameisennavigation`.

**systemd**: `deploy/ameisennav.service` runs the server as a sandboxed dynamic user:

```bash
sudo cmake --install build --prefix /usr/local
sudo install -Dm644 deploy/ameisennav.service /etc/systemd/system/ameisennav.service
sudo install -Dm644 config.cfg /etc/ameisennav/config.cfg
sudo systemctl daemon-reload && sudo systemctl enable --now ameisennav
```

## Robustness and security 🛡️

The server is meant to run next to untrusted bot processes and loads mesh files it didn't create:

- **Requests**: positions must be finite and within +-100000 yards, radii <= 5000, explore polygons <= 10000 yards
  and 256 points, map ids 0..65535, area costs 0 < cost <= 1e6. Malformed requests get a zero/empty answer and are
  only logged at debug level. Every request gets exactly one response.
- **Resources**: `dtNavMeshQuery` objects (A* node pools, ~2.7 MB each) are pooled per map and leased per request,
  memory follows the number of concurrently running requests, not clients x maps. `iMaxClients` caps threads,
  TCP keepalive frees threads of vanished peers.
- **Mesh files**: every Detour tile (ANP and MMAP) is validated before Detour sees it: sizes, every vertex/polygon/
  neighbour/detail/BV tree/off-mesh index, finite coordinates, BV quantization range. Navmesh parameters, tile
  sizes and the total decompressed size of an `.anp` are bounded. Broken tiles are skipped and counted.
- **Exporter input**: corrupt client files can't produce out of bounds reads, endless loops or NaN geometry for
  Recast (non-finite/absurd triangles and area rects are dropped).
- **Verification**: unit, end-to-end (tile pipeline, TCP server, full exports of synthetic client data) and C# client
  integration tests run with GCC, Clang, MSVC, ASan+UBSan (incl. float-cast-overflow) and ThreadSanitizer.
  libFuzzer targets cover tile validation + Detour queries, `.anp` loading, the exporter's ADT/WMO/M2/WDT/DBC
  parsers and the request handlers; inputs that ever crashed are kept in `tests/fuzz/regressions`. Fuzzing found
  (and this repo fixes) a null dereference in Detour's `closestPointOnDetailEdges` for tiles without detail edge
  flags, an endless `raycast` on inconsistent neighbour links, UB in miniz and an unbounded MH2O instance loop.
- **Binaries** are built with stack protector, `_FORTIFY_SOURCE=3`, stack clash protection, CET (IBT/SHSTK), PIE and
  full RELRO (`/guard:cf` + `/CETCOMPAT` with MSVC), the Docker image runs as an unprivileged user and the systemd
  unit in a sandbox.

```bash
# Fuzzing (Clang)
CC=clang CXX=clang++ cmake -S . -B build-fuzz -DANAV_BUILD_FUZZERS=ON -DANAV_BUILD_TESTS=OFF -DANAV_ENABLE_OPENMP=OFF
cmake --build build-fuzz
tests/fuzz/run_fuzzers.sh build-fuzz 300   # seconds per target
```

## Building 🛠️

Requirements: a C++20 compiler (MSVC 2022+, GCC 13+, Clang 17+), CMake 3.21+. The .NET projects need the .NET 10 SDK.

```bash
# Linux / Windows (CMake)
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build -C Release        # unit, end-to-end and exporter tests
tests/run_client_integration.sh build/bin/AmeisenNavigation.Server   # C# client against the real server
```

On Windows the Visual Studio solution (`AmeisenNavigation.sln`) works as well. CMake uses the prebuilt StormLib
from `dep/` with MSVC and fetches StormLib from GitHub everywhere else (`-DANAV_USE_BUNDLED_STORMLIB=OFF` forces that).

| CMake option | Default | |
| --- | --- | --- |
| `ANAV_BUILD_SERVER` / `ANAV_BUILD_EXPORTER` / `ANAV_BUILD_TESTS` | `ON` | Select targets |
| `ANAV_ENABLE_OPENMP` / `ANAV_ENABLE_LTO` | `ON` | Parallel loading/building, link time optimization |
| `ANAV_SANITIZE` | `OFF` | AddressSanitizer + UndefinedBehaviorSanitizer (GCC/Clang) |
| `ANAV_WARNINGS_AS_ERRORS` | `OFF` | `-Werror` / `/WX` for project code |
| `ANAV_HARDENING` | `ON` | Stack protector, FORTIFY_SOURCE, stack clash protection, CET, PIE, full RELRO / CFG |
| `ANAV_BUILD_FUZZERS` | `OFF` | libFuzzer targets (Clang only, instruments everything) |

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
| `tests/` | Unit and end-to-end tests: a synthetic world through the real tile pipeline and TCP server, full exports of synthetic client data (MPQ, DBC, WDT, ADT, WMO, M2), C# client integration, fuzzers (`tests/fuzz`) |
| `deploy/` | Docker entrypoint and compose file, systemd unit |

## Credits 🙌

- TrinityCore - [GitHub](https://github.com/TrinityCore/TrinityCore)
- recastnavigation - [GitHub](https://github.com/recastnavigation/recastnavigation)
- StormLib - [GitHub](https://github.com/ladislav-zezula/StormLib)
- miniz - [GitHub](https://github.com/richgel999/miniz)
