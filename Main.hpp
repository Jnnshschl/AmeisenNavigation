#pragma once

#include <msclr/marshal_cppstd.h>

#include "../AmeisenNavigation/ameisennavigation.h"

namespace AmeisenNavigationWrapper
{
    public ref class AmeisenNav : System::IDisposable
    {
    private:
        AmeisenNavigation* pAmeisenNav;
        int mMaxPolyPath;
        int mMaxPointPath;

    public:
        /// <summary>
        /// Create a instance of AmeisenNav to use Pathfinding.
        ///
        /// Use GetPath or GetPathAsArray to build a Path from
        /// one position to another.
        /// </summary>
        /// <param name="mmapFolder">The folder containing the extracted mmaps</param>
        AmeisenNav(System::String^ mmapFolder, int maxPolyPathLenght, int maxPointPathLenght)
            : pAmeisenNav(new AmeisenNavigation(msclr::interop::marshal_as<std::string>(mmapFolder), maxPolyPathLenght, maxPointPathLenght)),
            mMaxPolyPath(maxPolyPathLenght),
            mMaxPointPath(maxPointPathLenght)
        {
        }

        ~AmeisenNav()
        {
            this->!AmeisenNav();
        }

        !AmeisenNav()
        {
            delete pAmeisenNav;
        }

        void LoadMap(int mapId)
        {
            pAmeisenNav->LoadMmapsForContinent(mapId);
        }

        bool IsMapLoaded(int mapId)
        {
            return pAmeisenNav->IsMmapLoaded(mapId);
        }

        bool CastMovementRay(int mapId, float start[], float end[])
        {
            return pAmeisenNav->CastMovementRay(mapId, start, end);
        }

        array<float>^ MoveAlongSurface(int mapId, float start[], float end[])
        {
            array<float>^ position = gcnew array<float>(3);
            pin_ptr<float> pPosition = &position[0];

            pAmeisenNav->MoveAlongSurface(mapId, start, end, (Vector3*)pPosition);
            return position;
        }

        array<float>^ GetRandomPoint(int mapId)
        {
            array<float>^ position = gcnew array<float>(3);
            pin_ptr<float> pPosition = &position[0];

            pAmeisenNav->GetRandomPoint(mapId, (Vector3*)pPosition);
            return position;
        }

        array<float>^ GetRandomPointAround(int mapId, float start[], float maxRadius)
        {
            array<float>^ position = gcnew array<float>(3);
            pin_ptr<float> pPosition = &position[0];

            pAmeisenNav->GetRandomPointAround(mapId, start, maxRadius, (Vector3*)pPosition);
            return position;
        }

        /// <summary>
        /// Use this method to generate a path, it will
        /// be retuned as an 1D array formatted like:
        /// [ x1, y1, z1, x2, y2, z2, ...]
        /// </summary>
        /// <param name="mapmapId_id">The MapId as an integer 0 => Eastern Kingdoms...</param>
        /// <param name="start">The position to start at</param>
        /// <param name="end">The position to go to</param>
        /// <param name="pathSize">Count of waypoints inside the list, remember each waypoint has 3 floats</param>
        /// <returns>Pointer to the array of waypoints</returns>
        array<float>^ GetPath(int mapId, float start[], float end[], int* pathSize)
        {
            Vector3* path = new Vector3[mMaxPointPath];
            pAmeisenNav->GetPath(mapId, start, end, path, pathSize);

            array<float>^ tempPath = gcnew array<float>(*pathSize * 3);
            pin_ptr<float> pTempPath = &tempPath[0];

            // copy the array to trim it
            memcpy(pTempPath, path, sizeof(Vector3) * (*pathSize));

            delete[] path;
            return tempPath;
        }
    };
}
