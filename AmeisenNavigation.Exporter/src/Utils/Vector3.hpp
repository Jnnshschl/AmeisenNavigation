#pragma once

// Single Vector3 definition shared across all projects.
// This redirect exists so that relative includes from the exporter still resolve.
#include "../../../AmeisenNavigation/src/Utils/Vector3.hpp"

#include <cmath>

/// Exporter input coordinates beyond this are garbage from corrupt files (the world spans +-17067 yards): geometry
/// and area rects using them are dropped before Recast and the rect grid convert them to ints.
constexpr float MAX_PLAUSIBLE_COORDINATE = 1e6f;

inline bool IsPlausibleCoordinate(float v) noexcept
{
    return std::isfinite(v) && std::fabs(v) <= MAX_PLAUSIBLE_COORDINATE;
}
