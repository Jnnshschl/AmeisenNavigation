#pragma once

#include <set>
#include <chrono>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <unordered_map>
#include <vector>
#include <random>

#include "../../recastnavigation/Recast/Include/Recast.h"
#include "../../recastnavigation/Detour/Include/DetourNavMesh.h"
#include "../../recastnavigation/Detour/Include/DetourNavMeshBuilder.h"
#include "../../recastnavigation/Detour/Include/DetourNavMeshQuery.h"

#include "../../AmeisenNavigation.Pack/src/Anp.hpp"

#include "Dbc/Dbc.hpp"
#include "Mpq/MpqManager.hpp"
#include "Utils/Structure.hpp"
#include "Utils/Tri.hpp"
#include "Utils/Vector3.hpp"
#include "Wow/Adt.hpp"
#include "Wow/LiquidType.hpp"
#include "Wow/Wdt.hpp"

#define START_TIMER(name) auto name = std::chrono::high_resolution_clock::now()

#define STOP_TIMER(name, msg) std::cout << msg \
                               << std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now() - name).count() / 1000.0f \
                               << " ms" << std::endl

static float frand() noexcept
{
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<float> dis(0.0f, 1.0f);
    return dis(gen);
}

constexpr auto GAME_DIR = "C:\\Spiele\\World of Warcraft 3.3.5a\\Data\\";
constexpr auto OUTPUT_DIR = "C:\\Temp\\Meshes\\";
constexpr auto MESH_WORLDUNIT = TILESIZE / 500;

static bool GenerateNavmeshTile(rcContext& rcCtx, rcConfig& cfg, int x, int y, Structure& terrain, rcPolyMesh*& pmesh, rcPolyMeshDetail*& dmesh) noexcept;
