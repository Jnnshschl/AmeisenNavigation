#pragma once

#include "RectGrid.hpp"
#include "Vector3.hpp"

/// City area coverage: one rect per MCNK chunk (CHUNKSIZE x CHUNKSIZE) within a city area.
/// The tile processor upgrades TERRAIN_GROUND spans inside these rects to TERRAIN_CITY.
///
/// City detection uses AreaTable.dbc flags:
///   0x08 = Capital city (Stormwind, Orgrimmar, etc.)
///   0x20 = Slave capital / secondary town
struct CityMap : RectMap<RdRect>
{
    /// Add a city rectangle from WoW NW and SE corner positions (WoW coords).
    void AddRect(const Vector3& wowNW, const Vector3& wowSE) { Add(RdRect::FromWow(wowNW, wowSE, 0.01f)); }
};
