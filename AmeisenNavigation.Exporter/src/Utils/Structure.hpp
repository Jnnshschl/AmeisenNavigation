#pragma once

#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <mutex>
#include <algorithm>

#include "Vector3.hpp"
#include "Tri.hpp"

struct Structure
{
    std::mutex mutex;
    std::vector<Vector3> verts;
    std::vector<Tri> tris;
    std::vector<TriAreaId> triTypes;
    float bbMin[3]{ 0.0f };
    float bbMax[3]{ 0.0f };

    inline auto Verts() noexcept { return reinterpret_cast<float*>(&verts[0]); }
    inline auto Tris() noexcept { return reinterpret_cast<int*>(&tris[0]); }
    inline auto AreaIds() noexcept { return reinterpret_cast<unsigned char*>(&triTypes[0]); }

    inline void AddVert(const Vector3& vert) noexcept { verts.emplace_back(vert); }
    inline void AddTri(const Tri& tri, const TriAreaId& t) noexcept { tris.emplace_back(tri); triTypes.emplace_back(t); }

    inline void Append(const Structure& other) noexcept
    {
        int triOffset = verts.size();
        verts.append_range(other.verts);

        for (size_t i = 0; i < other.tris.size(); ++i)
        {
            const auto& t = other.tris[i];
            tris.push_back(Tri{ t.a + triOffset, t.b + triOffset, t.c + triOffset });
            triTypes.push_back(other.triTypes[i]);
        }
    }

    /// <summary>
    /// This method removes, unused and duplicate verts and tris from the structure.
    /// </summary>
    inline void Clean() noexcept
    {
        std::vector<bool> isVertexUsed(verts.size(), false);

        for (const auto& tri : tris)
        {
            isVertexUsed[tri.a] = true;
            isVertexUsed[tri.b] = true;
            isVertexUsed[tri.c] = true;
        }

        std::unordered_map<Vector3, int, Vector3::Hash> uniqueVerticesMap;
        std::vector<Vector3> filteredVertices;
        std::vector<int> vertexIndexMap(verts.size(), -1);

        for (size_t i = 0, newIndex = 0; i < verts.size(); ++i)
        {
            if (isVertexUsed[i])
            {
                const auto& insertionResult = uniqueVerticesMap.insert({ verts[i], newIndex });

                if (insertionResult.second)
                {
                    filteredVertices.emplace_back(verts[i]);
                    vertexIndexMap[i] = newIndex++;
                }
                else
                {
                    vertexIndexMap[i] = insertionResult.first->second;
                }
            }
        }

        for (auto& tri : tris)
        {
            tri.a = vertexIndexMap[tri.a];
            tri.b = vertexIndexMap[tri.b];
            tri.c = vertexIndexMap[tri.c];
        }

        std::unordered_set<Tri, Tri::Hash> uniqueSet;
        std::vector<TriAreaId> uniqueTriangleAreaIds;

        for (unsigned int i = 0; i < tris.size(); ++i)
        {
            const auto& tri = tris[i];
            auto [_, inserted] = uniqueSet.insert(tri);

            if (inserted)
            {
                uniqueTriangleAreaIds.emplace_back(triTypes[i]);
            }
        }

        verts = std::move(filteredVertices);
        tris = std::vector<Tri>(std::make_move_iterator(uniqueSet.begin()), std::make_move_iterator(uniqueSet.end()));
        triTypes = std::move(uniqueTriangleAreaIds);
    }

    inline void CleanOutOfBounds(float* bmin, float* bmax) noexcept
    {
        std::vector<Vector3> filteredVertices;
        std::vector<int> vertexIndexMapping(verts.size(), -1);

        for (size_t i = 0; i < verts.size(); ++i)
        {
            const Vector3& vertex = verts[i];

            if (vertex.x >= bmin[0] && vertex.x <= bmax[0]
                && vertex.y >= bmin[1] && vertex.y <= bmax[1]
                && vertex.z >= bmin[2] && vertex.z <= bmax[2])
            {
                filteredVertices.push_back(vertex);
                vertexIndexMapping[i] = filteredVertices.size() - 1;
            }
        }

        std::vector<Tri> filteredTriangles;

        for (const Tri& triangle : tris)
        {
            auto v1 = vertexIndexMapping[triangle.a];
            auto v2 = vertexIndexMapping[triangle.b];
            auto v3 = vertexIndexMapping[triangle.c];

            if (v1 != -1 && v2 != -1 && v3 != -1)
            {
                filteredTriangles.emplace_back(Tri{ v1, v2, v3 });
            }
        }

        verts = std::move(filteredVertices);
        tris = std::move(filteredTriangles);
    }

    inline void ExportDebugObjFile(const char* filePath) noexcept
    {
        std::fstream objFstream;
        objFstream << std::fixed << std::showpoint;
        objFstream << std::setprecision(8);
        objFstream.open(filePath, std::fstream::out);

        for (const auto& v3 : verts)
        {
            objFstream << "v " << v3.x << " " << v3.y << " " << v3.z << "\n";
        }

        for (const auto& tri : tris)
        {
            objFstream << "f " << tri.a + 1 << " " << tri.b + 1 << " " << tri.c + 1 << "\n";
        }

        objFstream.close();
    }
};
