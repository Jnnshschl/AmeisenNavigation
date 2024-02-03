#pragma once

#include "../Utils/Misc.hpp"

#pragma pack(push, 1)
struct MD20
{
    unsigned char magic[4];
    unsigned int size;
    unsigned int lengthModelName;
    unsigned int offsetName;
    unsigned int modelFlags;
    unsigned int countGlobalSequences;
    unsigned int offsetGlobalSequences;
    unsigned int countAnimations;
    unsigned int offsetAnimations;
    unsigned int countAnimationLookup;
    unsigned int offsetAnimationLookup;
    unsigned int countBones;
    unsigned int offsetBones;
    unsigned int countKeyBoneLookup;
    unsigned int offsetKeyBoneLookup;
    unsigned int countVertices;
    unsigned int offsetVertices;
    unsigned int countViews;
    unsigned int countColors;
    unsigned int offsetColors;
    unsigned int countTextures;
    unsigned int offsetTextures;
    unsigned int countTransparency;
    unsigned int offsetTransparency;
    unsigned int countUvAnimation;
    unsigned int offsetUvAnimation;
    unsigned int countTexReplace;
    unsigned int offsetTexReplace;
    unsigned int countRenderFlags;
    unsigned int offsetRenderFlags;
    unsigned int countBoneLookup;
    unsigned int offsetBoneLookup;
    unsigned int countTexLookup;
    unsigned int offsetTexLookup;
    unsigned int countTexUnits;
    unsigned int offsetTexUnits;
    unsigned int countTransLookup;
    unsigned int offsetTransLookup;
    unsigned int countUvAnimLookup;
    unsigned int offsetUvAnimLookup;
    Vector3 vertexBox[2];
    float vertexRadius;
    Vector3 boundingBox[2];
    float boundingRadius;
    unsigned int countBoundingTriangles;
    unsigned int offsetBoundingTriangles;
    unsigned int countBoundingVertices;
    unsigned int offsetBoundingVertices;
    unsigned int countBoundingNormals;
    unsigned int offsetBoundingNormals;
};
#pragma pack(pop)

struct M2
{
    unsigned char* Data;
    unsigned int Size;

public:
    inline const MD20* Md20() const noexcept { return reinterpret_cast<MD20*>(Data); };

    inline const Vector3* Vertex(size_t i) const noexcept { return reinterpret_cast<Vector3*>(Data + Md20()->offsetBoundingVertices + (sizeof(Vector3) * i)); }
    inline const unsigned short* Tri(size_t i) const noexcept { return reinterpret_cast<unsigned short*>(Data + Md20()->offsetBoundingTriangles + (sizeof(unsigned short) * i)); }

    inline bool IsCollideable() const noexcept
    {
        return Md20()->offsetBoundingNormals > 0 && Md20()->offsetBoundingVertices > 0 &&
            Md20()->offsetBoundingTriangles > 0 && Md20()->boundingRadius > 0.0f;
    }
};
