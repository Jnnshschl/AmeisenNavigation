#pragma once

#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <unordered_map>

#include "../INavSource.hpp"
#include "../../Utils/Logger.hpp"
#include "MmapFormat.hpp"
#include "MmapTileHeader.hpp"

class MmapNavSource : public INavSource
{
	std::filesystem::path MmapFolder;
	MmapFormat Format;
	std::mutex MapInsertMutex; // protects NavMeshMap structural modifications (insert/find)
	std::unordered_map<size_t, std::pair<std::mutex, dtNavMesh*>> NavMeshMap;

	std::map<MmapFormat, std::pair<std::string_view, std::string_view>> MmapFormatPatterns
	{
		{ MmapFormat::TC335A, std::make_pair("{:03}.mmap", "{:03}{:02}{:02}.mmtile") },
		{ MmapFormat::SF548, std::make_pair("{:04}.mmap", "{:04}_{:02}_{:02}.mmtile") },
	};

public:
	MmapNavSource(const char* mmapFolder, MmapFormat format = MmapFormat::UNKNOWN)
		: MmapFolder(mmapFolder),
		Format(format),
		NavMeshMap{}
	{
		if (format == MmapFormat::UNKNOWN)
		{
			Format = TryDetectMmapFormat();
		}
	}

	~MmapNavSource()
	{
		for (auto& [id, mtxNavMesh] : NavMeshMap)
		{
			const std::lock_guard lock(mtxNavMesh.first);

			if (mtxNavMesh.second)
			{
				dtFreeNavMesh(mtxNavMesh.second);
			}
		}
	}

	constexpr inline MmapFormat GetFormat() const noexcept { return Format; }

	virtual dtNavMesh* Get(size_t mapId) noexcept override
	{
		try
		{
			LoadMmaps(mapId);
			const std::lock_guard<std::mutex> insertLock(MapInsertMutex);
			auto it = NavMeshMap.find(mapId);
			return it != NavMeshMap.end() ? it->second.second : nullptr;
		}
		catch (...) { return nullptr; }
	}

private:
	bool LoadMmaps(size_t mapId) noexcept
	{
		try {
		// Ensure the map entry exists under the insert mutex before locking the per-map mutex.
		{
			const std::lock_guard<std::mutex> insertLock(MapInsertMutex);
			NavMeshMap[mapId]; // default-constructs entry if absent
		}

		const std::lock_guard<std::mutex> lock(NavMeshMap[mapId].first);

		if (NavMeshMap[mapId].second) { return true; }

		if (!MmapFormatPatterns.contains(Format))
			return false;

		const auto& filenameFormat = MmapFormatPatterns.at(Format);

		std::filesystem::path mmapFile(MmapFolder);
		const int mid = static_cast<int>(mapId);
		std::string filename = std::vformat(filenameFormat.first, std::make_format_args(mid));
		mmapFile.append(filename);

		if (!std::filesystem::exists(mmapFile))
		{
			return false;
		}

		std::ifstream mmapStream;
		mmapStream.open(mmapFile, std::ifstream::binary);

		dtNavMeshParams params{};
		mmapStream.read(reinterpret_cast<char*>(&params), sizeof(dtNavMeshParams));
		mmapStream.close();

		NavMeshMap[mapId].second = dtAllocNavMesh();

		if (!NavMeshMap[mapId].second)
		{
			return false;
		}

		dtStatus initStatus = NavMeshMap[mapId].second->init(&params);

		if (dtStatusFailed(initStatus))
		{
			dtFreeNavMesh(NavMeshMap[mapId].second);
			return false;
		}

        constexpr int TILE_GRID_SIZE = 64;

#pragma omp parallel for schedule(dynamic)
		for (int i = 0; i < TILE_GRID_SIZE * TILE_GRID_SIZE; ++i)
		{
			const auto x = i / TILE_GRID_SIZE;
			const auto y = i % TILE_GRID_SIZE;

			std::filesystem::path mmapTileFile(MmapFolder);
			mmapTileFile.append(std::vformat(filenameFormat.second, std::make_format_args(mid, x, y)));

			if (!std::filesystem::exists(mmapTileFile))
			{
				continue;
			}


			std::ifstream mmapTileStream;
			mmapTileStream.open(mmapTileFile, std::ifstream::binary);

			MmapTileHeader mmapTileHeader{};
			mmapTileStream.read(reinterpret_cast<char*>(&mmapTileHeader), sizeof(MmapTileHeader));

			if (mmapTileHeader.mmapMagic != MMAP_MAGIC)
			{
				continue;
			}

			if (mmapTileHeader.mmapVersion < MMAP_VERSION)
			{
				continue;
			}

			void* mmapTileData = malloc(mmapTileHeader.size);
			if (!mmapTileData)
			{
				continue;
			}
			mmapTileStream.read(static_cast<char*>(mmapTileData), mmapTileHeader.size);
			mmapTileStream.close();

#pragma omp critical(addMmapTile)
			{
				dtStatus addTileStatus = NavMeshMap[mapId].second->addTile
				(
					static_cast<unsigned char*>(mmapTileData),
					mmapTileHeader.size,
					DT_TILE_FREE_DATA,
					0,
					nullptr
				);

				if (dtStatusFailed(addTileStatus))
				{
					free(mmapTileData);
				}
			}
		}

		return true;
		} catch (const std::exception& e) { LogE("LoadMmaps failed: ", e.what()); return false; }
		  catch (...) { LogE("LoadMmaps failed: unknown exception"); return false; }
	}

	inline MmapFormat TryDetectMmapFormat() noexcept
	{
		try
		{
			int zero = 0;
			int twentyseven = 27;
			auto args = std::make_format_args(zero, twentyseven, twentyseven);

			for (const auto& [format, pattern] : MmapFormatPatterns)
			{
				if (std::filesystem::exists(MmapFolder / std::vformat(pattern.second, args)))
				{
					return format;
				}
			}
		}
		catch (...) {}

		return MmapFormat::UNKNOWN;
	}
};
