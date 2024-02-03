#pragma once

#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <unordered_map>
#include <vector>

#include "Utils/Vector3.hpp"
#include "Utils/Tri.hpp"
#include "Dbc/Dbc.hpp"
#include "Mpq/MpqManager.hpp"
#include "Wow/Adt.hpp"
#include "Wow/LiquidType.hpp"
#include "Wow/Wdt.hpp"

constexpr auto GAME_DIR = "C:\\Spiele\\World of Warcraft 3.3.5a\\Data\\";

void ExportDebugObjFile(const std::vector<Vector3>& vertexes, const std::vector<Tri>& tris) noexcept;
