#include "NavServer.hpp"

#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <format>
#include <span>
#include <thread>
#include <vector>

namespace {
/// Copy a request out of the (possibly unaligned) receive buffer. Returns false if the packet is too small.
template <typename T>
bool ReadRequest(const void* data, int size, T& out) noexcept
{
    static_assert(std::is_trivially_copyable_v<T>);

    if (!data || size < static_cast<int>(sizeof(T)))
    {
        return false;
    }

    std::memcpy(&out, data, sizeof(T));
    return true;
}

bool SendVector(ClientHandler* handler, AnTcpMessageType type, const Vector3& v) noexcept
{
    return handler->SendData(type, &v, sizeof(Vector3));
}

bool SendZero(ClientHandler* handler, AnTcpMessageType type) noexcept { return SendVector(handler, type, Vector3{}); }

const char* MessageName(AnTcpMessageType type) noexcept
{
    switch (static_cast<MessageType>(type))
    {
        case MessageType::PATH: return "Path";
        case MessageType::MOVE_ALONG_SURFACE: return "MoveAlongSurface";
        case MessageType::RANDOM_POINT: return "RandomPoint";
        case MessageType::RANDOM_POINT_AROUND: return "RandomPointAround";
        case MessageType::CAST_RAY: return "CastRay";
        case MessageType::RANDOM_PATH: return "RandomPath";
        case MessageType::EXPLORE_POLY: return "ExplorePoly";
        case MessageType::CONFIGURE_FILTER: return "ConfigureFilter";
        case MessageType::GET_HEIGHT: return "GetHeight";
        case MessageType::GET_CONFIG: return "GetConfig";
        case MessageType::CAST_RAY_EX: return "CastRayEx";
        default: return "Unknown";
    }
}

void LogTooSmall(ClientHandler* handler, AnTcpMessageType type, int size, size_t expected)
{
    // Debug level: a misbehaving client could otherwise flood the log.
    LogD("[", handler->GetId(), "] ", MessageName(type), ": packet too small (", size, " < ", expected, ")");
}
} // namespace

NavServer::NavServer(const AmeisenNavConfig& config) : Cfg(config)
{
    AmeisenNavigationSettings settings;
    settings.meshFolder = std::filesystem::path(Cfg.mmapsPath);
    settings.useAnp = Cfg.useAnpFileFormat;
    settings.mmapFormat = ToMmapFormat(Cfg.mmapFormat);
    settings.customMmapPatterns = {Cfg.customMmapPattern, Cfg.customMmtilePattern};
    settings.maxPolyPath = Cfg.maxPolyPath;
    settings.maxPointPath = Cfg.maxPointPath;
    settings.maxSearchNodes = Cfg.maxSearchNodes;
    settings.factionDangerCost = Cfg.factionDangerCost;
    settings.waterCost = Cfg.waterCost;
    settings.badLiquidCost = Cfg.badLiquidCost;
    settings.roadCost = Cfg.roadCost;

    Navigation = std::make_unique<AmeisenNavigation>(settings);
    TcpServer = std::make_unique<AnTcpServer>(Cfg.ip, static_cast<unsigned short>(Cfg.port));
    TcpServer->SetMaxClients(static_cast<size_t>(Cfg.maxClients));
    TcpServer->SetIdleTimeout(std::chrono::seconds(Cfg.clientIdleTimeoutSec));

    RegisterCallbacks();
}

MmapFormat NavServer::ToMmapFormat(int configValue) noexcept
{
    switch (configValue)
    {
        case -1: return MmapFormat::CUSTOM;
        case 1: return MmapFormat::TC335A;
        case 2: return MmapFormat::SF548;
        default: return MmapFormat::UNKNOWN;
    }
}

void NavServer::ValidateConfig(AmeisenNavConfig& config, std::vector<std::string>& errors,
                               std::vector<std::string>& warnings)
{
    std::error_code ec;

    if (config.mmapsPath.empty() || !std::filesystem::is_directory(config.mmapsPath, ec))
    {
        errors.push_back(std::format("sMmapsPath is not a directory: \"{}\"", config.mmapsPath));
    }

    if (config.port < 0 || config.port > 65535)
    {
        errors.push_back("iPort has to be a value between 0 and 65535");
    }

    if (config.maxPolyPath <= 0)
    {
        errors.push_back("iMaxPolyPath has to be a value > 0");
    }

    if (config.maxSearchNodes <= 0 || config.maxSearchNodes > 65535)
    {
        errors.push_back("iMaxSearchNodes has to be a value between 1 and 65535");
    }

    if (config.maxClients < 0)
    {
        errors.push_back("iMaxClients has to be >= 0 (0 = unlimited)");
    }

    if (config.statsIntervalSec < 0)
    {
        errors.push_back("iStatsIntervalSec has to be >= 0 (0 = off)");
    }

    if (config.clientIdleTimeoutSec < 0)
    {
        errors.push_back("iClientIdleTimeoutSec has to be >= 0 (0 = never)");
    }

    if (config.maxPointPath < 2)
    {
        errors.push_back("iMaxPointPath has to be a value >= 2");
    }
    else if (static_cast<size_t>(config.maxPointPath) * sizeof(Vector3) + 1 > ANTCP_MAX_RESPONSE_SIZE)
    {
        errors.push_back("iMaxPointPath is too large for a single response");
    }

    if (config.mmapFormat < -1 || config.mmapFormat > 2)
    {
        errors.push_back("iMmapFormat has to be -1 (custom), 0 (auto), 1 (TC 3.3.5a) or 2 (SkyFire 5.4.8)");
    }

    if (config.mmapFormat == -1 && (config.customMmapPattern.empty() || config.customMmtilePattern.empty()))
    {
        errors.push_back("iMmapFormat=-1 requires sCustomMmapPattern and sCustomMmtilePattern");
    }

    if (config.bezierCurvePoints < 2)
    {
        warnings.push_back("iBezierCurvePoints too low, clamping to 2");
        config.bezierCurvePoints = 2;
    }

    if (config.catmullRomSplinePoints < 1)
    {
        warnings.push_back("iCatmullRomSplinePoints too low, clamping to 1");
        config.catmullRomSplinePoints = 1;
    }

    if (config.catmullRomSplineAlpha < 0.0f || config.catmullRomSplineAlpha > 1.0f)
    {
        warnings.push_back("fCatmullRomSplineAlpha has to be within [0, 1], clamping");
        config.catmullRomSplineAlpha = std::clamp(config.catmullRomSplineAlpha, 0.0f, 1.0f);
    }

    if (config.factionDangerCost < 0.01f)
    {
        warnings.push_back("fFactionDangerCost too low, clamping to 0.01");
        config.factionDangerCost = 0.01f;
    }

    const std::pair<const char*, float*> costs[]{
        {"fWaterCost", &config.waterCost}, {"fBadLiquidCost", &config.badLiquidCost}, {"fRoadCost", &config.roadCost}};

    for (const auto& cost : costs)
    {
        if (*cost.second < 0.01f)
        {
            warnings.push_back(std::format("{} too low, clamping to 0.01", cost.first));
            *cost.second = 0.01f;
        }
    }

    if (config.randomPathMaxDistance < 0.0f)
    {
        warnings.push_back("fRandomPathMaxDistance negative, clamping to 0");
        config.randomPathMaxDistance = 0.0f;
    }
}

void NavServer::PreloadMaps()
{
    for (const int mapId : Cfg.GetPreloadMaps())
    {
        if (!Navigation->PreloadMap(mapId))
        {
            LogW("Preload: no navmesh for map ", mapId);
        }
    }
}

void NavServer::RegisterCallbacks()
{
    auto& s = *TcpServer;

    s.SetOnClientConnected([this](ClientHandler* h) { OnClientConnect(h); });
    s.SetOnClientDisconnected([this](ClientHandler* h) { OnClientDisconnect(h); });

    // A connection flood would otherwise log one line per refused connection: at most one warning per 10s.
    s.SetOnClientRejected([this](size_t active) {
        using namespace std::chrono;
        const long long now = duration_cast<seconds>(steady_clock::now().time_since_epoch()).count();
        long long last = LastRejectLog.load(std::memory_order_relaxed);

        if ((last == 0 || now - last >= 10) && LastRejectLog.compare_exchange_strong(last, now))
        {
            LogW("Client limit reached (", active, " connected, iMaxClients=", Cfg.maxClients,
                 "), refusing new connections (", TcpServer->GetRejectedCount(), " refused so far)");
        }
    });

    // Every handler is timed for the stats log.
    const auto timed = [this](auto&& handler) {
        return [this, handler](ClientHandler* h, AnTcpMessageType t, const void* d, int sz) {
            const auto start = std::chrono::steady_clock::now();
            handler(h, t, d, sz);
            RecordRequest(t, static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
                                                       std::chrono::steady_clock::now() - start)
                                                       .count()));
        };
    };

    const auto add = [&](MessageType type, auto member) {
        s.AddCallback(static_cast<AnTcpMessageType>(type),
                      timed([this, member](ClientHandler* h, AnTcpMessageType t, const void* d, int sz) {
                          (this->*member)(h, t, d, sz);
                      }));
    };

    s.AddCallback(static_cast<AnTcpMessageType>(MessageType::PATH),
                  timed([this](ClientHandler* h, AnTcpMessageType t, const void* d, int sz) {
                      HandlePath(h, t, d, sz, PathType::STRAIGHT);
                  }));
    s.AddCallback(static_cast<AnTcpMessageType>(MessageType::RANDOM_PATH),
                  timed([this](ClientHandler* h, AnTcpMessageType t, const void* d, int sz) {
                      HandlePath(h, t, d, sz, PathType::RANDOM);
                  }));

    add(MessageType::MOVE_ALONG_SURFACE, &NavServer::HandleMoveAlongSurface);
    add(MessageType::CAST_RAY, &NavServer::HandleCastRay);
    add(MessageType::CAST_RAY_EX, &NavServer::HandleCastRayEx);
    add(MessageType::RANDOM_POINT, &NavServer::HandleRandomPoint);
    add(MessageType::RANDOM_POINT_AROUND, &NavServer::HandleRandomPointAround);
    add(MessageType::CONFIGURE_FILTER, &NavServer::HandleConfigureFilter);
    add(MessageType::GET_HEIGHT, &NavServer::HandleGetHeight);
    add(MessageType::GET_CONFIG, &NavServer::HandleGetConfig);
    add(MessageType::EXPLORE_POLY, &NavServer::HandleExplorePoly);
}

AnTcpError NavServer::Run() noexcept
{
    std::thread reporter;

    {
        const std::lock_guard lock(StatsMutex);
        StatsStop = false;
    }

    if (Cfg.statsIntervalSec > 0)
    {
        try
        {
            reporter = std::thread([this] { StatsLoop(); });
        }
        catch (...)
        {
            LogW("Failed to start the stats thread, request stats are disabled");
        }
    }

    const AnTcpError result = TcpServer->Run();

    {
        const std::lock_guard lock(StatsMutex);
        StatsStop = true;
    }

    StatsCv.notify_all();

    if (reporter.joinable())
    {
        reporter.join();
    }

    return result;
}

void NavServer::RecordRequest(AnTcpMessageType type, uint64_t micros) noexcept
{
    auto& stats = Stats[type % STATS_SLOTS];
    stats.count.fetch_add(1, std::memory_order_relaxed);
    stats.totalMicros.fetch_add(micros, std::memory_order_relaxed);

    uint64_t max = stats.maxMicros.load(std::memory_order_relaxed);

    while (micros > max && !stats.maxMicros.compare_exchange_weak(max, micros, std::memory_order_relaxed))
    {
    }
}

std::string NavServer::TakeStatsSummary(double seconds)
{
    std::string details;
    uint64_t total = 0;

    for (size_t i = 0; i < STATS_SLOTS; ++i)
    {
        const uint64_t count = Stats[i].count.exchange(0, std::memory_order_relaxed);
        const uint64_t micros = Stats[i].totalMicros.exchange(0, std::memory_order_relaxed);
        const uint64_t max = Stats[i].maxMicros.exchange(0, std::memory_order_relaxed);

        if (count == 0)
        {
            continue;
        }

        total += count;
        details += std::format(" | {} {} avg {:.2f}ms max {:.2f}ms", MessageName(static_cast<AnTcpMessageType>(i)),
                               count, static_cast<double>(micros) / static_cast<double>(count) / 1000.0,
                               static_cast<double>(max) / 1000.0);
    }

    if (total == 0)
    {
        return {};
    }

    return std::format("Stats {:.0f}s: {} requests ({:.1f}/s), {} clients, {} refused{}", seconds, total,
                       seconds > 0.0 ? static_cast<double>(total) / seconds : 0.0, TcpServer->GetClientCount(),
                       TcpServer->GetRejectedCount(), details);
}

void NavServer::StatsLoop()
{
    auto last = std::chrono::steady_clock::now();
    std::unique_lock lock(StatsMutex);

    while (!StatsCv.wait_for(lock, std::chrono::seconds(Cfg.statsIntervalSec), [this] { return StatsStop; }))
    {
        const auto now = std::chrono::steady_clock::now();
        lock.unlock();

        const std::string summary = TakeStatsSummary(std::chrono::duration<double>(now - last).count());
        last = now;

        if (!summary.empty())
        {
            LogI(summary);
        }

        lock.lock();
    }
}

void NavServer::OnClientConnect(ClientHandler* handler)
{
    LogI("Client connected: ", handler->GetIpAddress(), ":", handler->GetPort(), " (id ", handler->GetId(), ")");
    Navigation->NewClient(handler->GetId());
}

void NavServer::OnClientDisconnect(ClientHandler* handler)
{
    Navigation->FreeClient(handler->GetId());
    LogI("Client disconnected: ", handler->GetIpAddress(), ":", handler->GetPort(), " (id ", handler->GetId(), ")");
}

Path* NavServer::ApplyPathFlags(size_t clientId, int mapId, int flags, PathType pathType, Path& path, Path& scratch)
{
    Path* current = &path;
    Path* spare = &scratch;
    bool smoothed = false;

    if (HasFlag(flags, PathRequestFlags::SMOOTH_CHAIKIN) && current->pointCount >= 3)
    {
        Navigation->SmoothPathChaikinCurve(*current, *spare);
        smoothed = true;
    }
    else if (HasFlag(flags, PathRequestFlags::SMOOTH_CATMULLROM) && current->pointCount >= 3)
    {
        Navigation->SmoothPathCatmullRom(*current, *spare, Cfg.catmullRomSplinePoints, Cfg.catmullRomSplineAlpha);
        smoothed = true;
    }
    else if (HasFlag(flags, PathRequestFlags::SMOOTH_BEZIERCURVE) && current->pointCount >= 4)
    {
        Navigation->SmoothPathBezier(*current, *spare, Cfg.bezierCurvePoints);
        smoothed = true;
    }

    if (smoothed && spare->pointCount > 0)
    {
        std::swap(current, spare);
    }

    // Smoothed and randomized paths may leave the navmesh, validate them if requested.
    if ((smoothed || pathType == PathType::RANDOM) && current->pointCount > 0)
    {
        bool validated = false;

        if (HasFlag(flags, PathRequestFlags::VALIDATE_CPOP))
        {
            validated = Navigation->PostProcessClosestPointOnPoly(clientId, mapId, *current, *spare);
        }
        else if (HasFlag(flags, PathRequestFlags::VALIDATE_MAS))
        {
            validated = Navigation->PostProcessMoveAlongSurface(clientId, mapId, *current, *spare);
        }

        if (validated && spare->pointCount > 0)
        {
            std::swap(current, spare);
        }
    }

    return current;
}

void NavServer::HandlePath(ClientHandler* handler, AnTcpMessageType type, const void* data, int size,
                           PathType pathType)
{
    PathRequestData request{};

    if (!ReadRequest(data, size, request))
    {
        LogTooSmall(handler, type, size, sizeof(request));
        SendZero(handler, type);
        return;
    }

    const auto start = std::chrono::steady_clock::now();
    const auto client = Navigation->GetClient(handler->GetId());

    if (!client)
    {
        SendZero(handler, type);
        return;
    }

    Path& path = client->GetPathBuffer();
    Path& scratch = client->GetScratchPathBuffer();

    bool partial = false;
    bool ok = pathType == PathType::RANDOM
                  ? Navigation->GetRandomPath(handler->GetId(), request.mapId, request.start, request.end, path,
                                              Cfg.randomPathMaxDistance, &partial)
                  : Navigation->GetPath(handler->GetId(), request.mapId, request.start, request.end, path, &partial);

    if (ok && partial && HasFlag(request.flags, PathRequestFlags::REQUIRE_COMPLETE))
    {
        LogD("[", handler->GetId(), "] ", MessageName(type), ": end not reachable, partial path rejected");
        ok = false;
    }

    int pointCount = 0;

    if (ok && path.pointCount > 0)
    {
        const Path* result = ApplyPathFlags(handler->GetId(), request.mapId, request.flags, pathType, path, scratch);
        pointCount = result->pointCount;
        handler->SendData(type, result->points, static_cast<size_t>(result->pointCount) * sizeof(Vector3));
    }
    else
    {
        SendZero(handler, type);
    }

    LogD("[", handler->GetId(), "] ", MessageName(type), " map=", request.mapId, ok ? " ok" : " FAIL",
         " pts=", pointCount, " ",
         std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count(),
         "us");
}

void NavServer::HandleMoveAlongSurface(ClientHandler* handler, AnTcpMessageType type, const void* data, int size)
{
    MoveRequestData request{};

    if (!ReadRequest(data, size, request))
    {
        LogTooSmall(handler, type, size, sizeof(request));
        SendZero(handler, type);
        return;
    }

    Vector3 point;
    const bool ok =
        Navigation->MoveAlongSurface(handler->GetId(), request.mapId, request.start, request.end, point);
    SendVector(handler, type, ok ? point : Vector3{});
    LogD("[", handler->GetId(), "] MoveAlongSurface map=", request.mapId, ok ? " ok" : " FAIL");
}

void NavServer::HandleCastRay(ClientHandler* handler, AnTcpMessageType type, const void* data, int size)
{
    CastRayData request{};

    if (!ReadRequest(data, size, request))
    {
        LogTooSmall(handler, type, size, sizeof(request));
        SendZero(handler, type);
        return;
    }

    // Legacy semantics: the end position if the way is clear, zero if blocked.
    const bool clear = Navigation->CastMovementRay(handler->GetId(), request.mapId, request.start, request.end);
    SendVector(handler, type, clear ? request.end : Vector3{});
    LogD("[", handler->GetId(), "] CastRay map=", request.mapId, clear ? " clear" : " blocked");
}

void NavServer::HandleCastRayEx(ClientHandler* handler, AnTcpMessageType type, const void* data, int size)
{
    CastRayData request{};
    CastRayExResponse response{-1, 0.0f, Vector3{}, Vector3{}};

    if (!ReadRequest(data, size, request))
    {
        LogTooSmall(handler, type, size, sizeof(request));
        handler->SendDataPtr(type, &response);
        return;
    }

    RaycastResult result;
    const bool clear =
        Navigation->CastMovementRay(handler->GetId(), request.mapId, request.start, request.end, &result);

    if (clear || result.hit)
    {
        response.hit = result.hit ? 1 : 0;
        response.t = result.t;
        response.position = result.hitPosition;
        response.normal = result.hitNormal;
    }

    handler->SendDataPtr(type, &response);
}

void NavServer::HandleRandomPoint(ClientHandler* handler, AnTcpMessageType type, const void* data, int size)
{
    int mapId = 0;

    if (!ReadRequest(data, size, mapId))
    {
        LogTooSmall(handler, type, size, sizeof(mapId));
        SendZero(handler, type);
        return;
    }

    Vector3 point;
    const bool ok = Navigation->GetRandomPoint(handler->GetId(), mapId, point);
    SendVector(handler, type, ok ? point : Vector3{});
}

void NavServer::HandleRandomPointAround(ClientHandler* handler, AnTcpMessageType type, const void* data, int size)
{
    RandomPointAroundData request{};

    if (!ReadRequest(data, size, request))
    {
        LogTooSmall(handler, type, size, sizeof(request));
        SendZero(handler, type);
        return;
    }

    Vector3 point;
    const bool ok =
        Navigation->GetRandomPointAround(handler->GetId(), request.mapId, request.start, request.radius, point);
    SendVector(handler, type, ok ? point : Vector3{});
}

void NavServer::HandleConfigureFilter(ClientHandler* handler, AnTcpMessageType type, const void* data, int size)
{
    const auto reply = [&](bool result) { handler->SendDataVar(type, result); };

    ConfigureFilterHeader header{};

    if (!ReadRequest(data, size, header))
    {
        LogTooSmall(handler, type, size, sizeof(header));
        reply(false);
        return;
    }

    const int count = header.filterConfigCount;
    const size_t expectedSize = sizeof(ConfigureFilterHeader) + static_cast<size_t>(std::max(count, 0)) * sizeof(FilterConfig);

    if (count < 0 || count > MAX_FILTER_CONFIGS || static_cast<size_t>(size) < expectedSize)
    {
        LogD("[", handler->GetId(), "] ConfigureFilter: invalid entry count ", count);
        reply(false);
        return;
    }

    const auto client = Navigation->GetClient(handler->GetId());

    if (!client)
    {
        reply(false);
        return;
    }

    AreaCost costs[MAX_FILTER_CONFIGS]{};
    const auto* entries = static_cast<const unsigned char*>(data) + sizeof(ConfigureFilterHeader);

    for (int i = 0; i < count; ++i)
    {
        FilterConfig entry{};
        std::memcpy(&entry, entries + static_cast<size_t>(i) * sizeof(FilterConfig), sizeof(FilterConfig));
        costs[i] = {entry.areaId, entry.cost};
    }

    const bool ok = client->ConfigureQueryFilter(header.state, std::span<const AreaCost>(costs, count));

    if (!ok)
    {
        LogD("[", handler->GetId(), "] ConfigureFilter: rejected (state=", static_cast<int>(header.state),
             ", entries=", count, ")");
    }

    LogD("[", handler->GetId(), "] ConfigureFilter state=", static_cast<int>(header.state), " entries=", count);
    reply(ok);
}

void NavServer::HandleGetHeight(ClientHandler* handler, AnTcpMessageType type, const void* data, int size)
{
    GetHeightData request{};

    if (!ReadRequest(data, size, request))
    {
        LogTooSmall(handler, type, size, sizeof(request));
        SendZero(handler, type);
        return;
    }

    Vector3 point;
    const bool ok = Navigation->GetHeight(handler->GetId(), request.mapId, request.position, point);
    SendVector(handler, type, ok ? point : Vector3{});
}

void NavServer::HandleGetConfig(ClientHandler* handler, AnTcpMessageType type, const void* /*data*/, int /*size*/)
{
    const auto& path = Cfg.mmapsPath;

    GetConfigResponseHeader header{};
    // Report the effective (detected) format using the config's numbering.
    switch (Navigation->GetMmapFormat())
    {
        case MmapFormat::TC335A: header.mmapFormat = 1; break;
        case MmapFormat::SF548: header.mmapFormat = 2; break;
        case MmapFormat::CUSTOM: header.mmapFormat = -1; break;
        default: header.mmapFormat = 0; break;
    }

    header.useAnpFileFormat = Cfg.useAnpFileFormat ? 1 : 0;
    header.pathLength = static_cast<int>(path.size());

    const std::string_view version = AMEISENNAV_VERSION;
    GetConfigResponseTrailer trailer{};
    trailer.protocolVersion = PROTOCOL_VERSION;
    trailer.maxPointPath = Cfg.maxPointPath;
    trailer.versionLength = static_cast<int>(version.size());

    std::vector<char> buffer;
    buffer.reserve(sizeof(header) + path.size() + sizeof(trailer) + version.size());

    const auto append = [&](const void* bytes, size_t count) {
        buffer.insert(buffer.end(), static_cast<const char*>(bytes), static_cast<const char*>(bytes) + count);
    };

    append(&header, sizeof(header));
    append(path.data(), path.size());
    append(&trailer, sizeof(trailer));
    append(version.data(), version.size());

    handler->SendData(type, buffer.data(), buffer.size());
}

void NavServer::HandleExplorePoly(ClientHandler* handler, AnTcpMessageType type, const void* data, int size)
{
    ExplorePolyRequestHeader request{};

    if (!ReadRequest(data, size, request))
    {
        LogTooSmall(handler, type, size, sizeof(request));
        SendZero(handler, type);
        return;
    }

    const int count = request.pointCount;

    if (count < 3 || count > MAX_EXPLORE_POLYGON_POINTS
        || static_cast<size_t>(size) < sizeof(request) + static_cast<size_t>(count) * sizeof(Vector3))
    {
        LogD("[", handler->GetId(), "] ExplorePoly: invalid polygon (", count, " points, ", size, " bytes)");
        SendZero(handler, type);
        return;
    }

    const auto start = std::chrono::steady_clock::now();
    const auto client = Navigation->GetClient(handler->GetId());

    if (!client)
    {
        SendZero(handler, type);
        return;
    }

    std::vector<Vector3> polygon(static_cast<size_t>(count));
    std::memcpy(polygon.data(), static_cast<const char*>(data) + sizeof(request), polygon.size() * sizeof(Vector3));

    Path& path = client->GetPathBuffer();
    Path& scratch = client->GetScratchPathBuffer();
    ExploreResult result;

    const bool ok = Navigation->ExplorePolygon(handler->GetId(), request.mapId, request.start, polygon,
                                               request.spacing, path, &result);

    if (ok)
    {
        const Path* route =
            ApplyPathFlags(handler->GetId(), request.mapId, request.flags, PathType::STRAIGHT, path, scratch);
        handler->SendData(type, route->points, static_cast<size_t>(route->pointCount) * sizeof(Vector3));
    }
    else
    {
        SendZero(handler, type);
    }

    LogD("[", handler->GetId(), "] ExplorePoly map=", request.mapId, ok ? " ok" : " FAIL", " waypoints=",
         result.reached, "/", result.waypoints, " pts=", path.pointCount, " ",
         std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count(),
         "us");
}
