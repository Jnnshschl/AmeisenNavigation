#pragma once

#include <chrono>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <random>
#include <set>
#include <unordered_map>
#include <vector>

constexpr auto AMEISENNAV_VERSION = "1.8.4.0";

#include "../../recastnavigation/Detour/Include/DetourNavMesh.h"
#include "../../recastnavigation/Detour/Include/DetourNavMeshBuilder.h"
#include "../../recastnavigation/Detour/Include/DetourNavMeshQuery.h"
#include "../../recastnavigation/Recast/Include/Recast.h"

#include "../../AmeisenNavigation.Pack/src/Anp.hpp"
#include <Utils/Logger.hpp>

#include "Dbc/Dbc.hpp"
#include "Mpq/MpqManager.hpp"
#include "Processors/AdtTileProcessor.hpp"
#include "Utils/Structure.hpp"
#include "Utils/Tri.hpp"
#include "Utils/Vector3.hpp"
#include "Wow/Adt.hpp"
#include "Wow/AdtChunkExtractor.hpp"
#include "Wow/LiquidType.hpp"
#include "Wow/RoadDetector.hpp"
#include "Wow/Wdt.hpp"


#define START_TIMER(name) auto name = std::chrono::high_resolution_clock::now()

#define STOP_TIMER(name, msg)                                         \
    auto end_time_##name = std::chrono::high_resolution_clock::now(); \
    Logger::Log(                                                      \
        Logger::Level::Timer, msg, " ",                               \
        std::format("{:.2f} ms",                                      \
                    std::chrono::duration_cast<std::chrono::microseconds>(end_time_##name - name).count() / 1000.0f));

static float frand() noexcept
{
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<float> dis(0.0f, 1.0f);
    return dis(gen);
}
