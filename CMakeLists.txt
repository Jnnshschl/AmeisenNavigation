cmake_minimum_required(VERSION 3.21)

project(AmeisenNavigation
    VERSION 1.9.0
    DESCRIPTION "Navigation server and navmesh exporter for World of Warcraft bots"
    LANGUAGES C CXX)

# ── Options ──────────────────────────────────────────────────────────────────

option(ANAV_BUILD_SERVER    "Build the AmeisenNavigation.Server executable"          ON)
option(ANAV_BUILD_EXPORTER  "Build the AmeisenNavigation.Exporter (needs StormLib)"  ON)
option(ANAV_BUILD_TESTS     "Build the test suite"                                    ${PROJECT_IS_TOP_LEVEL})
option(ANAV_ENABLE_LTO      "Enable link time optimization for release builds"       ON)
option(ANAV_ENABLE_OPENMP   "Use OpenMP for parallel tile loading/building"          ON)
option(ANAV_WARNINGS_AS_ERRORS "Treat compiler warnings in project code as errors"   OFF)
option(ANAV_SANITIZE        "Build with address + undefined behavior sanitizers"     OFF)

option(ANAV_USE_BUNDLED_STORMLIB "MSVC: link the prebuilt StormLib from dep/ instead of building it" ON)
set(ANAV_STORMLIB_TAG "v9.31" CACHE STRING "StormLib git tag used when fetching it")

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

if(ANAV_ENABLE_LTO)
    include(CheckIPOSupported)
    check_ipo_supported(RESULT ANAV_IPO_SUPPORTED OUTPUT ANAV_IPO_ERROR LANGUAGES C CXX)
    if(ANAV_IPO_SUPPORTED)
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELWITHDEBINFO ON)
    endif()
endif()

find_package(Threads REQUIRED)

if(ANAV_ENABLE_OPENMP)
    find_package(OpenMP COMPONENTS CXX)
endif()

# Common settings for first-party targets.
add_library(anav_options INTERFACE)
target_compile_definitions(anav_options INTERFACE
    $<$<PLATFORM_ID:Windows>:NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS>)

if(MSVC)
    target_compile_options(anav_options INTERFACE /W4 /permissive- /utf-8 /Zc:__cplusplus /MP
        /wd4201  # nonstandard extension: nameless struct/union (Vector3)
        /wd4324  # structure was padded due to alignment specifier
        $<$<BOOL:${ANAV_WARNINGS_AS_ERRORS}>:/WX>)
else()
    target_compile_options(anav_options INTERFACE -Wall -Wextra -Wno-missing-field-initializers
        $<$<BOOL:${ANAV_WARNINGS_AS_ERRORS}>:-Werror>)
endif()

if(ANAV_SANITIZE AND NOT MSVC)
    target_compile_options(anav_options INTERFACE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(anav_options INTERFACE -fsanitize=address,undefined)
endif()

if(OpenMP_CXX_FOUND)
    target_link_libraries(anav_options INTERFACE OpenMP::OpenMP_CXX)
else()
    # Without OpenMP the parallel loops simply run sequentially.
    target_compile_options(anav_options INTERFACE $<IF:$<CXX_COMPILER_ID:MSVC>,/wd4068,-Wno-unknown-pragmas>)
endif()

# ── recastnavigation (vendored, DT_POLYREF64 is hard-enabled in DetourNavMesh.h) ──

file(GLOB ANAV_RECAST_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/recastnavigation/Recast/Source/*.cpp)
file(GLOB ANAV_DETOUR_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/recastnavigation/Detour/Source/*.cpp)

add_library(anav_recast STATIC ${ANAV_RECAST_SOURCES} ${ANAV_DETOUR_SOURCES})
target_include_directories(anav_recast SYSTEM PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/recastnavigation/Recast/Include
    ${CMAKE_CURRENT_SOURCE_DIR}/recastnavigation/Detour/Include)
if(ANAV_SANITIZE AND NOT MSVC)
    target_compile_options(anav_recast PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
endif()

# ── AmeisenNavigation.Pack (ANP file format: miniz + tile container) ──

add_library(anav_pack STATIC ${CMAKE_CURRENT_SOURCE_DIR}/AmeisenNavigation.Pack/src/miniz/miniz.c)
target_include_directories(anav_pack PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/AmeisenNavigation.Pack/src)
# ANP archives are read from / written to memory, the stdio file API of miniz isn't needed.
target_compile_definitions(anav_pack PUBLIC MINIZ_NO_STDIO)
target_link_libraries(anav_pack PUBLIC anav_recast)
if(OpenMP_CXX_FOUND)
    target_link_libraries(anav_pack PUBLIC OpenMP::OpenMP_CXX)
endif()

# ── AmeisenNavigation (core pathfinding library) ──

add_library(anav_core STATIC ${CMAKE_CURRENT_SOURCE_DIR}/AmeisenNavigation/src/AmeisenNavigation.cpp)
target_include_directories(anav_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/AmeisenNavigation/src)
target_link_libraries(anav_core PUBLIC anav_pack anav_recast Threads::Threads PRIVATE anav_options)

# ── AnTCP.Server (cross-platform TCP message server) ──

add_library(antcp_server STATIC ${CMAKE_CURRENT_SOURCE_DIR}/AnTCP.Server/src/AnTcpServer.cpp)
target_include_directories(antcp_server PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/AnTCP.Server/src)
target_link_libraries(antcp_server PUBLIC Threads::Threads PRIVATE anav_options)
if(WIN32)
    target_link_libraries(antcp_server PUBLIC ws2_32)
endif()

# ── AmeisenNavigation.Server ──

if(ANAV_BUILD_SERVER OR ANAV_BUILD_TESTS)
    add_library(anav_server_lib STATIC ${CMAKE_CURRENT_SOURCE_DIR}/AmeisenNavigation.Server/src/NavServer.cpp)
    target_include_directories(anav_server_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/AmeisenNavigation.Server/src)
    target_link_libraries(anav_server_lib PUBLIC anav_core antcp_server PRIVATE anav_options)
endif()

if(ANAV_BUILD_SERVER)
    add_executable(AmeisenNavigation.Server ${CMAKE_CURRENT_SOURCE_DIR}/AmeisenNavigation.Server/src/Main.cpp)
    if(WIN32)
        target_sources(AmeisenNavigation.Server PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/AmeisenNavigation.Server/AmeisenNavigation.Server.rc)
    endif()
    target_link_libraries(AmeisenNavigation.Server PRIVATE anav_server_lib anav_options)
    install(TARGETS AmeisenNavigation.Server RUNTIME DESTINATION bin)
endif()

# ── AmeisenNavigation.Exporter ──

# Header-only exporter pipeline (tile processor, area marking, WoW formats).
# Kept separate from the executable so the tests can drive the pipeline with synthetic geometry.
add_library(anav_exporter_core INTERFACE)
target_include_directories(anav_exporter_core INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/AmeisenNavigation.Exporter/src)
target_link_libraries(anav_exporter_core INTERFACE anav_pack anav_core anav_recast)

if(ANAV_BUILD_EXPORTER)
    set(ANAV_STORMLIB_TARGET "")
    set(ANAV_BUNDLED_STORMLIB_DIR ${CMAKE_CURRENT_SOURCE_DIR}/dep/StormLib)

    if(ANAV_USE_BUNDLED_STORMLIB AND MSVC AND EXISTS ${ANAV_BUNDLED_STORMLIB_DIR}/lib/x64/StormLibRUS.lib
       AND CMAKE_SIZEOF_VOID_P EQUAL 8)
        # Prebuilt static StormLib shipped in dep/ (Unicode, static CRT not required).
        add_library(anav_stormlib STATIC IMPORTED)
        set_target_properties(anav_stormlib PROPERTIES
            IMPORTED_LOCATION_RELEASE ${ANAV_BUNDLED_STORMLIB_DIR}/lib/x64/StormLibRUS.lib
            IMPORTED_LOCATION_DEBUG ${ANAV_BUNDLED_STORMLIB_DIR}/lib/x64/StormLibDUS.lib
            IMPORTED_CONFIGURATIONS "RELEASE;DEBUG"
            MAP_IMPORTED_CONFIG_RELWITHDEBINFO RELEASE
            MAP_IMPORTED_CONFIG_MINSIZEREL RELEASE
            INTERFACE_INCLUDE_DIRECTORIES ${ANAV_BUNDLED_STORMLIB_DIR}/include
            INTERFACE_COMPILE_DEFINITIONS "_UNICODE;UNICODE")
        set(ANAV_STORMLIB_TARGET anav_stormlib)
    else()
        include(FetchContent)
        set(STORM_SKIP_INSTALL ON CACHE BOOL "" FORCE)
        set(STORM_BUILD_TESTS OFF CACHE BOOL "" FORCE)
        set(STORM_USE_BUNDLED_LIBRARIES ON CACHE BOOL "" FORCE)
        set(STORM_UNICODE ON CACHE BOOL "" FORCE)
        FetchContent_Declare(stormlib
            GIT_REPOSITORY https://github.com/ladislav-zezula/StormLib.git
            GIT_TAG ${ANAV_STORMLIB_TAG}
            GIT_SHALLOW TRUE)
        # Keep StormLib a static library regardless of the parent's BUILD_SHARED_LIBS.
        set(ANAV_SAVED_BUILD_SHARED_LIBS ${BUILD_SHARED_LIBS})
        set(BUILD_SHARED_LIBS OFF)
        FetchContent_MakeAvailable(stormlib)
        set(BUILD_SHARED_LIBS ${ANAV_SAVED_BUILD_SHARED_LIBS})
        target_include_directories(storm INTERFACE $<BUILD_INTERFACE:${stormlib_SOURCE_DIR}/src>)
        if(WIN32)
            # StormLib is built as Unicode (STORM_UNICODE), its TCHAR API must be seen as wchar_t by users too.
            target_compile_definitions(storm INTERFACE UNICODE _UNICODE)
        endif()
        set(ANAV_STORMLIB_TARGET storm)
    endif()

    add_executable(AmeisenNavigation.Exporter ${CMAKE_CURRENT_SOURCE_DIR}/AmeisenNavigation.Exporter/src/Main.cpp)
    if(WIN32)
        target_sources(AmeisenNavigation.Exporter PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/AmeisenNavigation.Exporter/AmeisenNavigation.Exporter.rc)
    endif()
    target_link_libraries(AmeisenNavigation.Exporter PRIVATE anav_exporter_core ${ANAV_STORMLIB_TARGET} anav_options)
    install(TARGETS AmeisenNavigation.Exporter RUNTIME DESTINATION bin)
endif()

# ── Tests ──

if(ANAV_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
