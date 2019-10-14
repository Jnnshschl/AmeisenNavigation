#pragma once

#include <msclr/marshal_cppstd.h>

#include "../AmeisenNavigation/ameisennavigation.h"

using namespace System;
using namespace System::Runtime::InteropServices;

namespace AmeisenNavigationWrapper
{
	public ref class AmeisenNav
	{
	private:
		AmeisenNavigation* ameisen_nav;

	public:
		/// <summary>
		/// Create a instance of AmeisenNav to use Pathfinding.
		///
		/// Use GetPath or GetPathAsArray to build a Path from
		/// one position to another.
		/// </summary>
		/// <param name="mmap_dir">The folder containing the extracted mmaps</param>
		AmeisenNav(String^ mmap_dir)
		{
			ameisen_nav = new AmeisenNavigation(msclr::interop::marshal_as<std::string>(mmap_dir));
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
		float* GetPath(int map_id, float start[], float end[], int* path_size)
		{
			float* path;
			ameisen_nav->GetPath(map_id, start, end, &path, path_size);

			return path;
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
		/// <returns>Array of waypoints (x, y, z)</returns>
		array<float>^ GetPathAsArray(int map_id, float start[], float end[], int* path_size)
		{
			float* path = GetPath(map_id, start, end, path_size);

			array<float>^ temp_path = gcnew array<float>(*path_size * 3);
			for (int i = 0; i < *path_size * 3; i += 3)
			{
				temp_path[i] = path[i];
				temp_path[i + 1] = path[i + 1];
				temp_path[i + 2] = path[i + 2];
			}

			return temp_path;
		}
	};
}
