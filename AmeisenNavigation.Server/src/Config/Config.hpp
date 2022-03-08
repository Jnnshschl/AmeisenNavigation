#pragma once

#include <map>
#include <string>
#include <fstream>
#include <sstream>
#include <filesystem>

constexpr auto CONFIG_CHAR_BOOL = 'b';
constexpr auto CONFIG_CHAR_INT = 'i';
constexpr auto CONFIG_CHAR_CHAR = 'c';
constexpr auto CONFIG_CHAR_STRING = 's';
constexpr auto CONFIG_CHAR_FLOAT = 'f';
constexpr auto CONFIG_CHAR_DELIMITER = '=';

struct AmeisenNavConfig
{
private:
    std::map<std::string, void*> Map
    {
        { "fCatmullRomSplineAlpha", &catmullRomSplineAlpha },
        { "fRandomPathMaxDistance", &randomPathMaxDistance },
        { "iCatmullRomSplinePoints", &catmullRomSplinePoints },
        { "iMaxPolyPath", &maxPolyPath },
        { "iMaxSearchNodes", &maxSearchNodes },
        { "iPort", &port },
        { "iClientVersion", &clientVersion },
        { "sIp", &ip },
        { "sMmapsPath", &mmapsPath },
    };

public:
    float catmullRomSplineAlpha = 1.0f;
    float randomPathMaxDistance = 1.5f;
    int catmullRomSplinePoints = 4;
    int maxPolyPath = 512;
    int maxSearchNodes = 65535;
    int port = 47110;
    int clientVersion = static_cast<int>(CLIENT_VERSION::V335A);
    std::string ip = "127.0.0.1";
    std::string mmapsPath = "C:\\shady stuff\\mmaps\\";

    void Save(const std::filesystem::path& path)
    {
        std::ofstream outputFile(path);

        for (auto const& x : Map)
        {
            switch (x.first[0])
            {
            case CONFIG_CHAR_BOOL: outputFile << x.first << CONFIG_CHAR_DELIMITER << static_cast<unsigned int>(*static_cast<bool*>(x.second)) << std::endl; break;
            case CONFIG_CHAR_CHAR: outputFile << x.first << CONFIG_CHAR_DELIMITER << static_cast<unsigned int>(*static_cast<char*>(x.second)) << std::endl; break;
            case CONFIG_CHAR_FLOAT: outputFile << x.first << CONFIG_CHAR_DELIMITER << std::to_string(*static_cast<float*>(x.second)) << std::endl; break;
            case CONFIG_CHAR_INT: outputFile << x.first << CONFIG_CHAR_DELIMITER << *static_cast<int*>(x.second) << std::endl; break;
            case CONFIG_CHAR_STRING: outputFile << x.first << CONFIG_CHAR_DELIMITER << *static_cast<std::string*>(x.second) << std::endl; break;
            default: break;
            }
        }

        outputFile.close();
    }

    void Load(const std::filesystem::path& path)
    {
        std::ifstream input(path);
        std::string item;

        for (std::string line; std::getline(input, line);)
        {
            std::stringstream ss(line);
            std::vector<std::string> result;

            while (std::getline(ss, item, CONFIG_CHAR_DELIMITER))
            {
                result.push_back(item);
            }

            if (result.size() == 2 && Map.count(result[0]))
            {
                switch (result[0][0])
                {
                case CONFIG_CHAR_BOOL: *static_cast<bool*>(Map.at(result[0])) = std::stoi(result[1]) > 0; break;
                case CONFIG_CHAR_CHAR: *static_cast<char*>(Map.at(result[0])) = static_cast<char>(std::stoi(result[1])); break;
                case CONFIG_CHAR_FLOAT: *static_cast<float*>(Map.at(result[0])) = std::stof(result[1]); break;
                case CONFIG_CHAR_INT: *static_cast<int*>(Map.at(result[0])) = std::stoi(result[1]); break;
                case CONFIG_CHAR_STRING: *static_cast<std::string*>(Map.at(result[0])) = result[1]; break;
                default: break;
                }
            }
        }

        input.close();
    }
};