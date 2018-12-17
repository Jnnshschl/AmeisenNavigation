#pragma once

#include "../AmeisenNavigation/ameisennavigation.h"

using namespace System;
using namespace System::Runtime::InteropServices;

namespace AmeisenNavigationWrapper {
	static const char* string_to_char_array(String^ string)
	{
		const char* str = (const char*)(Marshal::StringToHGlobalAnsi(string)).ToPointer();
		return str;
	}

	public ref class AmeisenNav
	{
		AmeisenNavigation* ameisen_nav;

	public:
		AmeisenNav(String^ mmap_dir) { ameisen_nav = new AmeisenNavigation(std::string(string_to_char_array(mmap_dir))); }
		~AmeisenNav() { this->!AmeisenNav(); }
		!AmeisenNav() { delete ameisen_nav; }

		array<float>^ GetPath(int map_id, float start_x, float start_y, float start_z, float end_x, float end_y, float end_z, int* path_size)
		{
			float start[] = { start_x, start_y, start_z };
			float end[] = { end_x, end_y, end_z };

			float* path;
			ameisen_nav->GetPath(map_id, start, end, &path, path_size);
			array<float>^ temp_path = gcnew array<float>(*path_size * 3);

			for (int i = 0; i < *path_size * 3; i+=3)
			{
				temp_path[i] = path[i];
				temp_path[i+1] = path[i+1];
				temp_path[i+2] = path[i+2];
			}
			return temp_path;
		}
	};
}
