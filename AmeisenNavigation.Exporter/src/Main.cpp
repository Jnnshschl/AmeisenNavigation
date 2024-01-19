#include "Main.hpp"

int main()
{
    const std::string mpqFilter{ ".mpq" };
    std::vector<std::filesystem::path> mpqFiles{};
    std::vector<std::pair<SFILE_FIND_DATA, std::filesystem::path>> dbcFiles{};

    for (const auto& entry : std::filesystem::recursive_directory_iterator("C:\\Spiele\\World of Warcraft 3.3.5a\\Data\\"))
    {
        if (entry.is_regular_file())
        {
            const auto& path = entry.path();
            const auto& ext = path.extension().string();

            if (std::equal(mpqFilter.begin(), mpqFilter.end(), ext.begin(), [](char a, char b) { return std::tolower(a) == std::tolower(b); }))
            {
                mpqFiles.push_back(path);
            }
        }
    }

    std::sort(mpqFiles.begin(), mpqFiles.end(), NaturalCompare);

    for (const auto& mpqFile : mpqFiles)
    {
        std::cout << mpqFile << std::endl;
        continue;

        if (void* mpq; SFileOpenArchive(mpqFile.c_str(), 0, MPQ_OPEN_READ_ONLY, &mpq))
        {
            SFILE_FIND_DATA findData{};
            void* fileFind = SFileFindFirstFile(mpq, "*.dbc", &findData, nullptr);

            while (fileFind)
            {
                dbcFiles.push_back(std::make_pair(findData, mpqFile));

                if (!SFileFindNextFile(fileFind, &findData))
                {
                    break;
                }
            }

            SFileFindClose(fileFind);
            SFileCloseArchive(mpq);
        }
        else
        {
            std::cerr << "-> Failed to open MPQ file: " << mpqFile << std::endl;
        }
    }

    std::sort(dbcFiles.begin(), dbcFiles.end(), [](const auto& a, const auto& b) { return a.second > b.second; });

    for (const auto& dbcFile : dbcFiles)
    {
        std::cout << dbcFile.first.szPlainName << std::endl;
    }

    return 0;
}