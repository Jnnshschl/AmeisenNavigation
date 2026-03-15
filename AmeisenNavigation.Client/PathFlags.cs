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
    }
}
