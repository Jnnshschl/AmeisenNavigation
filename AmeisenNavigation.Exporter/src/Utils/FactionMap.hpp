#pragma once

#include "RectGrid.hpp"
#include "Tri.hpp"
#include "Vector3.hpp"

/// One MCNK chunk (CHUNKSIZE x CHUNKSIZE) that belongs to a faction controlled area.
struct FactionRect : RdRect
{
    AreaFaction faction; // Alliance or Horde (neutral chunks are not stored)
};

/// Faction coverage. The tile processor upgrades neutral area ids to their Alliance/Horde variant inside these rects.
struct FactionMap : RectMap<FactionRect>
{
    /// Add a faction rectangle from WoW NW and SE corner positions (WoW coords).
    /// faction: 1 = Alliance, 2 = Horde (0 = neutral is ignored).
    void AddRect(const Vector3& wowNW, const Vector3& wowSE, unsigned char faction)
    {
        if (faction != static_cast<unsigned char>(AreaFaction::Alliance)
            && faction != static_cast<unsigned char>(AreaFaction::Horde))
        {
            return;
        }

        FactionRect r;
        static_cast<RdRect&>(r) = RdRect::FromWow(wowNW, wowSE, 0.01f);
        r.faction = static_cast<AreaFaction>(faction);
        Add(r);
    }
};
