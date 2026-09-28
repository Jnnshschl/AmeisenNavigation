namespace AmeisenNavigation.Client
{
    /// <summary>
    /// Result of <see cref="AmeisenNavClient.CastRayEx"/>.
    /// </summary>
    /// <param name="IsValid">False if the request failed (no navmesh, start position off the mesh, connection error).</param>
    /// <param name="IsHit">True if a wall was hit before reaching the end position.</param>
    /// <param name="Fraction">Fraction of the way from start to end travelled before the hit (1 if clear).</param>
    /// <param name="Position">Hit position, or the end position if the way is clear.</param>
    /// <param name="Normal">Normal of the wall that was hit (zero if clear).</param>
    public readonly record struct RaycastHit(bool IsValid, bool IsHit, float Fraction, Vector3 Position, Vector3 Normal)
    {
        /// <summary>True if the request succeeded and nothing blocks the way.</summary>
        public bool IsClear => IsValid && !IsHit;
    }
}
