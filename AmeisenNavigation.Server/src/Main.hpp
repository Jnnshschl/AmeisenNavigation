#pragma once

#include "AnTcpServer.hpp"

#include "../../AmeisenNavigation/src/AmeisenNavigation.hpp"
#include "../../AmeisenNavigation/src/Vector3.hpp"

#include "Config/Config.hpp"

#include <filesystem>
#include <iostream>

constexpr auto AMEISENNAV_VERSION = "1.7.0.0";

enum class MessageType
{
    PATH,
    MOVE_ALONG_SURFACE,
    RANDOM_POINT,
    RANDOM_POINT_AROUND,
};

struct PathRequestData
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

int __stdcall SigIntHandler(unsigned long signal);

void PathCallback(ClientHandler* handler, char type, const void* data, int size);
void RandomPointCallback(ClientHandler* handler, char type, const void* data, int size);
void RandomPointAroundCallback(ClientHandler* handler, char type, const void* data, int size);
void MoveAlongSurfaceCallback(ClientHandler* handler, char type, const void* data, int size);

inline Vector3* SmoothPathChaikinCurve(Vector3* input, int inputSize, int* outputSize, int iterations)
{
    *outputSize = (inputSize * 2) - 1;
    Vector3* output = new Vector3[*outputSize];

    for (int i = 0; i < inputSize - 1; ++i)
    {
        int index = i * 2;

        output[index] = Vector3(0.75f * input[i].x + 0.25f * input[i + 1].x, 0.75f * input[i].y + 0.25f * input[i + 1].y, input[i].z);
        output[index + 1] = Vector3(0.25f * input[i].x + 0.75f * input[i + 1].x, 0.25f * input[i].y + 0.75f * input[i + 1].y, input[i + 1].z);
    }

    output[*outputSize - 1] = input[inputSize - 1];
    output[*outputSize - 2] = input[inputSize - 2];
    output[*outputSize - 3] = input[inputSize - 3];

    return iterations > 1 ? SmoothPathChaikinCurve(output, inputSize, outputSize, --iterations) : output;
}
