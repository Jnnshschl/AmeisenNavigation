#pragma once

/// Area IDs for ANP navmesh polygons (and compact heightfield spans during export).
///
/// The IDs come in triplets {neutral, Alliance, Horde} per surface category, so
///   faction  = (area - 1) % 3        (0 = neutral, 1 = Alliance, 2 = Horde)
///   neutral  = area - faction
///
/// IMPORTANT: The numeric ordering matters because Recast's span merge uses
/// rcMax(area1, area2) when two spans overlap. Higher values win. Liquid types
/// are placed AFTER terrain types so that water surfaces always take priority
/// over terrain during heightfield rasterization.
///
/// All values must be <= RC_WALKABLE_AREA (63). These values are part of the
/// wire/file format (clients configure costs per area id), never renumber them.
enum TriAreaId : unsigned char
{
    NO_TYPE,

    // ── Terrain / structure types (lower priority in span merge) ──

    TERRAIN_GROUND,
    ALLIANCE_TERRAIN_GROUND,
    HORDE_TERRAIN_GROUND,

    TERRAIN_ROAD,
    ALLIANCE_TERRAIN_ROAD,
    HORDE_TERRAIN_ROAD,

    TERRAIN_CITY,
    ALLIANCE_TERRAIN_CITY,
    HORDE_TERRAIN_CITY,

    WMO,
    ALLIANCE_WMO,
    HORDE_WMO,

    DOODAD,
    ALLIANCE_DOODAD,
    HORDE_DOODAD,

    // ── Liquid types (higher priority - wins over terrain in span merge) ──

    LIQUID_WATER,
    ALLIANCE_LIQUID_WATER,
    HORDE_LIQUID_WATER,

    LIQUID_OCEAN,
    ALLIANCE_LIQUID_OCEAN,
    HORDE_LIQUID_OCEAN,

    LIQUID_LAVA,
    ALLIANCE_LIQUID_LAVA,
    HORDE_LIQUID_LAVA,

    LIQUID_SLIME,
    ALLIANCE_LIQUID_SLIME,
    HORDE_LIQUID_SLIME,

    ANP_AREA_COUNT
};

/// Detour polygon flags used by ANP navmeshes (dtQueryFilter include/exclude masks).
enum TriFlag : unsigned char
{
    NAV_EMPTY = 0,
    NAV_LAVA_SLIME = 1 << 0,
    NAV_WATER = 1 << 1,
    NAV_GROUND = 1 << 2,
    NAV_ROAD = 1 << 3,
    NAV_ALLIANCE = 1 << 4,
    NAV_HORDE = 1 << 5,
};

enum class AreaFaction : unsigned char
{
    Neutral = 0,
    Alliance = 1,
    Horde = 2,
};

constexpr bool IsValidAnpArea(unsigned int area) noexcept { return area > NO_TYPE && area < ANP_AREA_COUNT; }

constexpr bool IsLiquidArea(unsigned int area) noexcept { return area >= LIQUID_WATER && area <= HORDE_LIQUID_SLIME; }

/// WMO or doodad geometry (bridges, buildings, docks, ...).
constexpr bool IsStructureArea(unsigned int area) noexcept { return area >= WMO && area <= HORDE_DOODAD; }

constexpr AreaFaction GetAreaFaction(unsigned int area) noexcept
{
    return IsValidAnpArea(area) ? static_cast<AreaFaction>((area - 1) % 3) : AreaFaction::Neutral;
}

/// Strip the faction from an area id (ALLIANCE_TERRAIN_ROAD -> TERRAIN_ROAD).
constexpr unsigned char GetNeutralArea(unsigned int area) noexcept
{
    return IsValidAnpArea(area) ? static_cast<unsigned char>(area - static_cast<unsigned int>(GetAreaFaction(area)))
                                : static_cast<unsigned char>(area);
}

/// Apply a faction to a neutral area id (TERRAIN_ROAD + Horde -> HORDE_TERRAIN_ROAD).
constexpr unsigned char WithFaction(unsigned int area, AreaFaction faction) noexcept
{
    return IsValidAnpArea(area) ? static_cast<unsigned char>(GetNeutralArea(area) + static_cast<unsigned int>(faction))
                                : static_cast<unsigned char>(area);
}

/// Detour polygon flags for an area id.
constexpr unsigned short AreaToPolyFlags(unsigned int area) noexcept
{
    if (!IsValidAnpArea(area))
    {
        return NAV_EMPTY;
    }

    unsigned short flags = NAV_EMPTY;

    switch (GetNeutralArea(area))
    {
        case LIQUID_LAVA:
        case LIQUID_SLIME:
            flags = NAV_LAVA_SLIME;
            break;
        case LIQUID_WATER:
        case LIQUID_OCEAN:
            flags = NAV_WATER;
            break;
        case TERRAIN_ROAD:
            flags = NAV_GROUND | NAV_ROAD;
            break;
        default: // ground, city, wmo, doodad
            flags = NAV_GROUND;
            break;
    }

    switch (GetAreaFaction(area))
    {
        case AreaFaction::Alliance:
            flags |= NAV_ALLIANCE;
            break;
        case AreaFaction::Horde:
            flags |= NAV_HORDE;
            break;
        default:
            break;
    }

    return flags;
}

static_assert(ANP_AREA_COUNT == 28, "ANP area ids are part of the file/wire format");
static_assert(GetNeutralArea(HORDE_LIQUID_SLIME) == LIQUID_SLIME);
static_assert(WithFaction(TERRAIN_CITY, AreaFaction::Alliance) == ALLIANCE_TERRAIN_CITY);
static_assert(AreaToPolyFlags(ALLIANCE_TERRAIN_ROAD) == (NAV_GROUND | NAV_ROAD | NAV_ALLIANCE));
static_assert(AreaToPolyFlags(HORDE_LIQUID_LAVA) == (NAV_LAVA_SLIME | NAV_HORDE));
