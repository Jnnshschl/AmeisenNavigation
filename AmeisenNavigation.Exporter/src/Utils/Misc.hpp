#pragma once

#include <algorithm>

#define GetSubChunk(m, s, t) FindSubChunk<t>(m, s, #t)

template<typename T>
T* FindSubChunk(unsigned char* memory, unsigned int size, const char chunkName[5])
{
    const char chunkNameRev[4]
    {
        chunkName[3],
        chunkName[2],
        chunkName[1],
        chunkName[0],
    };

    while (memory < memory + (size - 4))
    {
        if (!memcmp(memory, chunkNameRev, 4))
        {
            return reinterpret_cast<T*>(memory);
        }

        memory++;
    }

    return nullptr;
}
