#ifndef _H_VECTOR3
#define _H_VECTOR3

#include <iostream>

struct Vector3
{
    float x;
    float y;
    float z;

    Vector3() : x(0.0f), y(0.0f), z(0.0f) {}

    Vector3(float values[3]) : x(values[0]), y(values[1]), z(values[2]) {}

    Vector3(float x, float y, float z) : x(x), y(y), z(z) {}

    inline Vector3 operator+(Vector3 vec) const { return Vector3{ x + vec.x, y + vec.y, z + vec.z }; }
    inline Vector3 operator-(Vector3 vec) const { return Vector3{ x - vec.x, y - vec.y, z - vec.z }; }
    inline Vector3 operator*(Vector3 vec) const { return Vector3{ x * vec.x, y * vec.y, z * vec.z }; }
    inline Vector3 operator/(Vector3 vec) const { return Vector3{ x / vec.x, y / vec.y, z / vec.z }; }
    inline Vector3 operator+=(Vector3 vec) { x += vec.x; y += vec.y; z += vec.z; return *this; }
    inline Vector3 operator-=(Vector3 vec) { x -= vec.x; y -= vec.y; z -= vec.z; return *this; }
    inline Vector3 operator*=(Vector3 vec) { x *= vec.x; y *= vec.y; z *= vec.z; return *this; }
    inline Vector3 operator/=(Vector3 vec) { x /= vec.x; y /= vec.y; z /= vec.z; return *this; }

    inline Vector3 operator+(float a) const { return Vector3{ x + a, y + a, z + a }; }
    inline Vector3 operator-(float a) const { return Vector3{ x - a, y - a, z - a }; }
    inline Vector3 operator*(float a) const { return Vector3{ x * a, y * a, z * a }; }
    inline Vector3 operator/(float a) const { return Vector3{ x / a, y / a, z / a }; }
    inline Vector3 operator+=(float a) { x += a; y += a; z += a; return *this; }
    inline Vector3 operator-=(float a) { x -= a; y -= a; z -= a; return *this; }
    inline Vector3 operator*=(float a) { x *= a; y *= a; z *= a; return *this; }
    inline Vector3 operator/=(float a) { x /= a; y /= a; z /= a; return *this; }

    inline operator float* () { return reinterpret_cast<float*>(this); }
    inline operator const float* () const { return reinterpret_cast<const float*>(this); }

    inline friend std::ostream& operator<<(std::ostream& os, Vector3 const& vector) {
        return os << vector.x << ", " << vector.y << ", " << vector.z;
    }
};

#endif