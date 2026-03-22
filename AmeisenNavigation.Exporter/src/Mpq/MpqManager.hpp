#pragma once

#include <algorithm>
#include <filesystem>
#include <memory>
#include <unordered_map>

#include <Utils/Logger.hpp>

#define STORMLIB_NO_AUTO_LINK
#include <stormlib.h>

#include "FileSort.hpp"

class MpqManager
{
	const char* GameDir;
	std::vector<void*> Mpqs;
	std::vector<std::unique_ptr<unsigned char[]>> Allocations;

public:
	explicit MpqManager(const char* gameDir) noexcept
		: GameDir(gameDir),
		Mpqs()
	{
		const std::string mpqFilter{ ".mpq" };

		std::vector<std::filesystem::directory_entry> mpqFiles;

		for (const auto& p : std::filesystem::recursive_directory_iterator(gameDir))
		{
			if (p.is_regular_file())
			{
				const auto& ext = p.path().extension().string();

				if (std::ranges::equal(mpqFilter, ext, [](char a, char b) { return std::tolower(a) == std::tolower(b); }))
				{
					mpqFiles.emplace_back(p);
				}
			}
		}

		std::ranges::sort(mpqFiles, NaturalCompare);
		std::ranges::reverse(mpqFiles);
		Mpqs.resize(mpqFiles.size());

#pragma omp parallel for schedule(dynamic)
		for (int i = 0; i < mpqFiles.size(); ++i)
		{
			const auto& path = mpqFiles[i].path();

			if (void* mpq; SFileOpenArchive(path.c_str(), 0, MPQ_OPEN_READ_ONLY, &mpq))
			{
				Mpqs[i] = mpq;
			}
		}

		LogS(std::format("Loaded {} MPQ archives", Mpqs.size()));
	}

	~MpqManager() noexcept
	{
		Allocations.clear();

		for (void* mpq : Mpqs)
		{
			SFileCloseArchive(mpq);
		}
	}

	inline bool GetFile(const char* name, SFILE_FIND_DATA& resultFindData, void*& mpqHandle) noexcept
	{
		SFILE_FIND_DATA findData{ 0 };

		for (void* mpq : Mpqs)
		{
			if (void* fileFind = SFileFindFirstFile(mpq, name, &findData, nullptr))
			{
				resultFindData = findData;
				mpqHandle = mpq;
				SFileFindClose(fileFind);
				return true;
			}
		}

		return false;
	}

	inline unsigned char* GetFileContent(const char* name, unsigned int& bufferSize) noexcept
	{
		void* mpq{};
		SFILE_FIND_DATA findData{};

		if (GetFile(name, findData, mpq))
		{
			if (HANDLE hFile{}; SFileOpenFileEx(mpq, findData.cFileName, 0, &hFile))
			{
				bufferSize = findData.dwFileSize;

				if (bufferSize > 0)
				{
					auto buffer = std::make_unique<unsigned char[]>(bufferSize);

					if (SFileReadFile(hFile, buffer.get(), bufferSize, 0, 0)
						&& SFileCloseFile(hFile))
					{
						unsigned char* ptr = buffer.get();
						Allocations.push_back(std::move(buffer));
						return ptr;
					}
				}

				SFileCloseFile(hFile);
			}
		}

		return nullptr;
	}
};
