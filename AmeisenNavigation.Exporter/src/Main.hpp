#pragma once

#include <atomic>
#include <chrono>
#include <filesystem>
#include <format>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#ifdef _OPENMP
#include <omp.h>
#endif

#include <DetourNavMesh.h>
#include <Recast.h>

#include "../../AmeisenNavigation.Pack/src/Anp.hpp"
#include "../../AmeisenNavigation/src/Utils/Logger.hpp"

#include "Dbc/Dbc.hpp"
#include "Mpq/CachedFileReader.hpp"
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

constexpr auto AMEISENNAV_VERSION = "1.9.0.0";
