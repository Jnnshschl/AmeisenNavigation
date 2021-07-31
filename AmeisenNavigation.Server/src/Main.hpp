#pragma once

#include "AnTcpServer.hpp"

#include "../../AmeisenNavigation/src/AmeisenNavigation.hpp"
#include "../../AmeisenNavigation/src/Vector/Vector3.hpp"

#include "Config/Config.hpp"

#include <filesystem>
#include <iostream>

constexpr auto AMEISENNAV_VERSION = "1.7.1.0";

enum class MessageType
{
    PATH,
    MOVE_ALONG_SURFACE,
    RANDOM_POINT,
    RANDOM_POINT_AROUND,
};

enum class PathRequestFlags : int
{
    NONE = 0,
    CHAIKIN = 1,
    CATMULLROM = 2,
};

struct PathRequestData
{
    int mapId;
    int flags;
    Vector3 start;
    Vector3 end;
};

struct MoveRequestData
{
    int mapId;
    Vector3 start;
    Vector3 end;
};

struct RandomPointAroundData
{
    int mapId;
    Vector3 start;
    float radius;
};

inline AnTcpServer* Server = nullptr;
inline AmeisenNavigation* Nav = nullptr;
inline AmeisenNavConfig* Config = nullptr;

inline const Vector3 Vector3Zero = Vector3();

int __stdcall SigIntHandler(unsigned long signal);

void PathCallback(ClientHandler* handler, char type, const void* data, int size);
void RandomPointCallback(ClientHandler* handler, char type, const void* data, int size);
void RandomPointAroundCallback(ClientHandler* handler, char type, const void* data, int size);
void MoveAlongSurfaceCallback(ClientHandler* handler, char type, const void* data, int size);

#if defined(WIN32) || defined(WIN64)
constexpr int COLOR_WHITE = 7;
constexpr int COLOR_RED = 12;
constexpr int COLOR_YELLOW = 14;

inline void ChangeOutputColor(int color)
{
    SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), color);
}
#else
#define ChangeOutputColor(c)
#endif

inline void SmoothPathChaikinCurve(Vector3* input, int inputSize, std::vector<Vector3>* output)
{
    output->reserve(((inputSize - 2) * 2) + 2 + 2);
    output->push_back(input[0]);

    for (int i = 0; i < inputSize - 1; ++i)
    {
        output->push_back((input[i] * 0.75f) + (input[i + 1] * 0.25f));
        output->push_back((input[i] * 0.25f) + (input[i + 1] * 0.75f));
    }

    output->push_back(input[inputSize - 1]);
}

inline void SmoothPathCatmullRom(Vector3* input, int inputSize, std::vector<Vector3>* output, int points, float alpha)
{
    output->reserve(inputSize * points);
    output->push_back(input[0]);

    for (int i = 1; i < inputSize - 2; ++i)
    {
        const Vector3 p0(input[i - 1]);
        const Vector3 p1(input[i]);
        const Vector3 p2(input[i + 1]);
        const Vector3 p3(input[i + 2]);

        const float t0 = 0.0f;
        const float t1 = std::powf(std::powf(p1.x - p0.x, 2.0f) + std::powf(p1.y - p0.y, 2.0f) + std::powf(p1.z - p0.z, 2.0f), alpha * 0.5f) + t0;
        const float t2 = std::powf(std::powf(p2.x - p1.x, 2.0f) + std::powf(p2.y - p1.y, 2.0f) + std::powf(p2.z - p1.z, 2.0f), alpha * 0.5f) + t1;
        const float t3 = std::powf(std::powf(p3.x - p2.x, 2.0f) + std::powf(p3.y - p2.y, 2.0f) + std::powf(p3.z - p2.z, 2.0f), alpha * 0.5f) + t2;

        for (float t = t1; t < t2; t += ((t2 - t1) / (float)points))
        {
            Vector3 A1 = (p0 * (t1 - t) / (t1 - t0)) + (p1 * (t - t0) / (t1 - t0));
            Vector3 A2 = (p1 * (t2 - t) / (t2 - t1)) + (p2 * (t - t1) / (t2 - t1));
            Vector3 A3 = (p2 * (t3 - t) / (t3 - t2)) + (p3 * (t - t2) / (t3 - t2));

            Vector3 B1 = (A1 * (t2 - t) / (t2 - t0)) + (A2 * (t - t0) / (t2 - t0));
            Vector3 B2 = (A2 * (t3 - t) / (t3 - t1)) + (A3 * (t - t1) / (t3 - t1));
            output->push_back((B1 * (t2 - t) / (t2 - t1)) + (B2 * (t - t1) / (t2 - t1)));
        }
    }

    output->push_back(input[inputSize - 1]);
}