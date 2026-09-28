#pragma once

enum class MmapFormat
{
    UNKNOWN, // auto detect
    TC335A,  // TrinityCore 3.3.5a: "{:03}.mmap", "{:03}{:02}{:02}.mmtile"
    SF548,   // SkyFire 5.4.8 / newer TrinityCore: "{:04}.mmap", "{:04}_{:02}_{:02}.mmtile"
    CUSTOM,  // user supplied std::format patterns
};
