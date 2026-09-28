#pragma once

#include "RectGrid.hpp"
#include "Vector3.hpp"

/// Road coverage: one rect per terrain sub-cell whose dominant texture is a road texture.
struct RoadMap : RectMap<RdRect>
{
    /// Add a road rectangle from WoW NW and SE corner positions (WoW coords).
    void AddRect(const Vector3& wowNW, const Vector3& wowSE) { Add(RdRect::FromWow(wowNW, wowSE, 0.01f)); }
};
