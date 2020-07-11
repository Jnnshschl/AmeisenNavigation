#pragma once

#include <msclr/marshal_cppstd.h>

#include "../AmeisenNavigation/ameisennavigation.h"

using namespace System;
using namespace System::Runtime::InteropServices;

namespace AmeisenNavigationWrapper
{
	public ref class AmeisenNav : IDisposable
	{
	private:
		AmeisenNavigation* ameisen_nav;
		int maxPolyPath;
		int maxPointPath;

	public:
		/// <summary>
		/// Create a instance of AmeisenNav to use Pathfinding.
		///
		/// Use GetPath or GetPathAsArray to build a Path from
		/// one position to another.
		/// </summary>
		/// <param name="mmap_dir">The folder containing the extracted mmaps</param>
		AmeisenNav(String^ mmap_dir, int maxPolyPathLenght, int maxPointPathLenght)
			: ameisen_nav(new AmeisenNavigation(msclr::interop::marshal_as<std::string>(mmap_dir), maxPolyPathLenght, maxPointPathLenght)),
			maxPolyPath(maxPolyPathLenght),
			maxPointPath(maxPointPathLenght)
		{
		}

		~AmeisenNav()
		{
			this->!AmeisenNav();
		}

		!AmeisenNav()
		{
			delete ameisen_nav;
		}

		/// <summary>
		/// Preload the mmaps of the selected map into RAM
		/// </summary>
		/// <param name="map_id">The MapId as an integer 0 => Eastern Kingdoms...</param>
		void LoadMap(int map_id)
		{
			ameisen_nav->LoadMmapsForContinent(map_id);
		}

		bool IsMapLoaded(int map_id)
		{
			return ameisen_nav->IsMmapLoaded(map_id);
		}

		bool CastMovementRay(int map_id, float start[], float end[])
		{
			return ameisen_nav->CastMovementRay(map_id, start, end);
		}

		array<float>^ MoveAlongSurface(int map_id, float start[], float end[])
		{
			float positionToGoTo[3];
			ameisen_nav->MoveAlongSurface(map_id, start, end, reinterpret_cast<Vector3*>(positionToGoTo));

			array<float>^ position = gcnew array<float>(3);
			position[0] = positionToGoTo[0];
			position[1] = positionToGoTo[1];
			position[2] = positionToGoTo[2];

			return position;
		}

		array<float>^ GetRandomPoint(int map_id)
		{
			float positionToGoTo[3];
			ameisen_nav->GetRandomPoint(map_id, reinterpret_cast<Vector3*>(positionToGoTo));

			array<float>^ position = gcnew array<float>(3);
			position[0] = positionToGoTo[0];
			position[1] = positionToGoTo[1];
			position[2] = positionToGoTo[2];

			return position;
		}

		array<float>^ GetRandomPointAround(int map_id, float start[], float maxRadius)
		{
			float positionToGoTo[3];
			ameisen_nav->GetRandomPointAround(map_id, start, maxRadius, reinterpret_cast<Vector3*>(positionToGoTo));

			array<float>^ position = gcnew array<float>(3);
			position[0] = positionToGoTo[0];
			position[1] = positionToGoTo[1];
			position[2] = positionToGoTo[2];

			return position;
		}

		/// <summary>
		/// Use this method if you dont want to mess around
		/// with an unsafe pointer in your code, the path
		/// will be retuned as an 1D array formatted like:
		/// [ x1, y1, z1, x2, y2, z2, ...]
		/// </summary>
		/// <param name="map_id">The MapId as an integer 0 => Eastern Kingdoms...</param>
		/// <param name="start">The position to start at</param>
		/// <param name="end">The position to go to</param>
		/// <param name="path_size">Count of waypoints inside the list, remember each waypoint has 3 floats</param>
		/// <returns>Pointer to the array of waypoints</returns>
		array<float>^ GetPath(int map_id, float start[], float end[], int* path_size)
		{
			float* path = new float[maxPointPath * 3];
			ameisen_nav->GetPath(map_id, start, end, reinterpret_cast<Vector3*>(path), path_size);

			array<float>^ temp_path = gcnew array<float>(*path_size * 3);
			for (int i = 0; i < *path_size * 3; i += 3)
			{
				temp_path[i] = path[i];
				temp_path[i + 1] = path[i + 1];
				temp_path[i + 2] = path[i + 2];
			}

			delete[] path;
			return temp_path;
		}
	};
}
