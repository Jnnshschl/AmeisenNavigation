#include "Main.hpp"

int main() noexcept
{
    MpqManager mpqManager(GAME_DIR);
    CachedFileReader mpqReader(&mpqManager);

    std::vector<std::pair<unsigned int, std::string>> maps;

    if (Dbc* mapDbc = mpqReader.GetFileContent<Dbc>("DBFilesClient\\Map.dbc"))
    {
        for (unsigned int i = 0u; i < mapDbc->GetRecordCount(); ++i)
        {
            maps.push_back(std::make_pair(mapDbc->Read<unsigned int>(i, 0u), mapDbc->ReadString(i, 1u)));
        }
    }

    std::unordered_map<unsigned int, LiquidType> liquidTypes;

    if (Dbc* liquidTypeDbc = mpqReader.GetFileContent<Dbc>("DBFilesClient\\LiquidType.dbc"))
    {
        for (unsigned int i = 0u; i < liquidTypeDbc->GetRecordCount(); ++i)
        {
            liquidTypes[liquidTypeDbc->Read<unsigned int>(i, 0u)] = static_cast<LiquidType>(liquidTypeDbc->Read<unsigned int>(i, 3u));
        }
    }

    // #pragma omp parallel for schedule(dynamic)
    for (int mapIndex = 0; mapIndex < 1; ++mapIndex) // maps.size()
    {
        const auto& [id, name] = maps[mapIndex];
        const auto mapsPath = std::format("World\\Maps\\{}\\{}", name, name);
        const auto wdtPath = std::format("{}.wdt", mapsPath);

        if (Wdt* wdt = mpqReader.GetFileContent<Wdt>(wdtPath.c_str()))
        {
            std::vector<Vector3> verts;
            std::vector<Tri> tris;

            for (int i = 0; i < 1; ++i) // WDT_MAP_SIZE * WDT_MAP_SIZE
            {
                const auto x = 48; // i % WDT_MAP_SIZE;
                const auto y = 32; // i / WDT_MAP_SIZE;

                if (wdt->Main()->adt[x][y].exists)
                {
                    const auto adtPath = std::format("{}_{}_{}.adt", mapsPath, y, x);
                    unsigned char* adtData = nullptr;
                    unsigned int adtSize = 0;

                    std::cout << "[Maps] " << name << ": ADT(" << x << ", " << y << ") " << adtPath << std::endl;

                    if (Adt* adt = mpqReader.GetFileContent<Adt>(adtPath.c_str()))
                    {
                        for (int a = 0; a < ADT_CELLS_PER_GRID * ADT_CELLS_PER_GRID; ++a)
                        {
                            const int cx = a / ADT_CELLS_PER_GRID;
                            const int cy = a % ADT_CELLS_PER_GRID;

                            adt->GetTerrainVertsAndTris(cx, cy, verts, tris);
                            adt->GetLiquidVertsAndTris(cx, cy, verts, tris);
                        }

                        adt->GetWmoVertsAndTris(mpqReader, verts, tris);
                        adt->GetDoodadVertsAndTris(mpqReader, verts, tris);
                    }
                }
            }

            ExportDebugObjFile(verts, tris);
        }
    }

    return 0;
}

void ExportDebugObjFile(const std::vector<Vector3>& vertexes, const std::vector<Tri>& tris) noexcept
{
    std::cout << "Exporting OBJ file" << std::endl;
    std::fstream objFstream;
    objFstream << std::fixed << std::showpoint;
    objFstream << std::setprecision(8);
    objFstream.open("C:\\Users\\Jannis\\source\\repos\\recastnavigation\\RecastDemo\\Bin\\Meshes\\debug.obj", std::fstream::out);

    for (const auto& v3 : vertexes)
    {
        objFstream << "v " << v3.y << " " << v3.z << " " << v3.x << "\n";
    }

    for (const auto& tri : tris)
    {
        objFstream << "f " << tri.a + 1 << " " << tri.b + 1 << " " << tri.c + 1 << "\n";
    }

    objFstream.close();
}
