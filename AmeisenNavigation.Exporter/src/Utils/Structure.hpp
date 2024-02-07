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

    inline auto Verts() noexcept { return reinterpret_cast<float*>(&verts[0]); }
    inline auto Tris() noexcept { return reinterpret_cast<int*>(&tris[0]); }
    inline auto AreaIds() noexcept { return reinterpret_cast<unsigned char*>(&triTypes[0]); }

    inline void AddVert(const Vector3& vert) noexcept { verts.emplace_back(vert); }
    inline void AddTri(const Tri& tri, const TriAreaId& t) noexcept { tris.emplace_back(tri); triTypes.emplace_back(t); }

    /// <summary>
    /// This method removes, unused and duplicate verts and tris from the structure.
    /// </summary>
    inline void Clean() noexcept
    {
        std::vector<bool> isVertexUsed(verts.size(), false);
        std::unordered_map<Vector3, int, Vector3::Hash> uniqueVerticesMap;
        std::unordered_set<Tri, Tri::Hash> uniqueTrianglesSet;

        for (const auto& tri : tris)
        {
            isVertexUsed[tri.a] = true;
            isVertexUsed[tri.b] = true;
            isVertexUsed[tri.c] = true;
            uniqueTrianglesSet.insert(tri);
        }

        std::vector<Vector3> filteredVertices;
        std::vector<int> vertexIndexMap(verts.size(), -1);

        for (size_t i = 0, newIndex = 0; i < verts.size(); ++i)
        {
            if (isVertexUsed[i])
            {
                auto insertionResult = uniqueVerticesMap.insert({ verts[i], newIndex });

                if (insertionResult.second)
                {
                    filteredVertices.push_back(verts[i]);
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

        verts = std::move(filteredVertices);

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

        tris.clear();

        for (const auto& tri : uniqueSet)
        {
            tris.emplace_back(tri);
        }

        triTypes = uniqueTriangleAreaIds;
    }
};
