using System;

namespace AmeisenNavigation.Client
{
    /// <summary>
    /// Path smoothing and validation flags. Combine with bitwise OR.
    /// At most one smoothing flag and one validation flag should be set.
    /// </summary>
    [Flags]
    public enum PathFlags
    {
        None = 0,
        SmoothChaikin = 1 << 0,
        SmoothCatmullRom = 1 << 1,
        SmoothBezier = 1 << 2,
        ValidateClosestPointOnPoly = 1 << 3,
        ValidateMoveAlongSurface = 1 << 4,

        /// <summary>
        /// Fail (return null) instead of returning a path that ends as close as possible to an unreachable
        /// end position. Requires protocol version 2 (server 1.9+), older servers ignore it.
        /// </summary>
        RequireComplete = 1 << 5,
    }
}
