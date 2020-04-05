#ifndef _H_VECTOR3
#define _H_VECTOR3

#include <iostream>

struct Vector3
{
	float x, y, z;

	Vector3()
		: x(0.F), y(0.F), z(0.F) {}

	Vector3(float values[3])
		: x(values[0]), y(values[1]), z(values[2]) {}

	Vector3(float x, float y, float z)
		: x(x), y(y), z(z) {}

	friend std::ostream& operator<<(std::ostream& os, Vector3 const& vector) {
		return os << "[" << vector.x << ", " << vector.y << ", " << vector.z << "]";
	}
};

#endif