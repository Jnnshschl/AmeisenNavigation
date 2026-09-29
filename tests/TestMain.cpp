#include "TestFramework.hpp"

#include "Utils/Logger.hpp"

int main(int argc, char** argv)
{
    Logger::Initialize();
    return Test::RunAll(argc > 1 ? argv[1] : "");
}
