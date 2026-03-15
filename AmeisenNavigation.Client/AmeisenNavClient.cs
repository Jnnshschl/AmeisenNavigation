using AnTCP.Client;
using System;
using System.IO;
using System.Net.Sockets;
using System.Threading;

namespace AmeisenNavigation.Client
{
    /// <summary>
    /// Client for the AmeisenNavigation pathfinding server.
    ///
    /// <para>Usage:</para>
    /// <code>
    /// using var nav = new AmeisenNavClient("127.0.0.1", 47110);
    /// nav.Connected += () => Console.WriteLine("Connected!");
    /// nav.Disconnected += () => Console.WriteLine("Lost connection");
    /// nav.TryConnect();
    /// nav.SetClientState(ClientState.NormalAlliance);
    /// nav.SetAreaCosts(ground: 1f, road: 0.75f, water: 1.6f, badLiquid: 4f, hordeMult: 3f);
    /// nav.ApplyFilter();
    ///
    /// var path = nav.GetPath(0, start, end);
    /// </code>
    /// </summary>
    public sealed class AmeisenNavClient : IDisposable
    {
        private enum MessageType : byte
        {
            Path,
            MoveAlongSurface,
            RandomPoint,
            RandomPointAround,
            CastRay,
            RandomPath,
            ExplorePoly,
            ConfigureFilter,
        }

        private readonly AnTcpClient _client;
        private readonly object _lock = new();

        private ClientState _state;
        private readonly float[] _areaCosts = new float[28]; // index 0 unused, 1-27 = area costs
        private bool _wasConnected;

        // ── Events ────────────────────────────────────────────────────────

        /// <summary>Raised when a connection (or reconnection) to the server succeeds.</summary>
        public event Action? Connected;

        /// <summary>Raised when the server connection is lost.</summary>
        public event Action? Disconnected;

        /// <summary>Raised before each reconnect attempt. The int parameter is the attempt number (1-based).</summary>
        public event Action<int>? Reconnecting;

        // ── Configuration ─────────────────────────────────────────────────

        /// <summary>
        /// When true, the client will automatically attempt to reconnect when a
        /// network error is detected during a pathfinding call. Default: true.
        /// </summary>
        public bool AutoReconnect { get; set; } = true;

        /// <summary>
        /// Milliseconds to wait between reconnect attempts. Default: 1000.
        /// </summary>
        public int ReconnectDelayMs { get; set; } = 1000;

        /// <summary>
        /// Maximum number of reconnect attempts before giving up.
        /// Set to 0 for infinite retries. Default: 5.
        /// </summary>
        public int MaxReconnectAttempts { get; set; } = 5;

        /// <summary>
        /// True when area costs or client state have changed since the last <see cref="ApplyFilter"/> call.
        /// </summary>
        public bool IsFilterDirty { get; private set; }

        public bool IsConnected => _client.IsConnected;

        public AmeisenNavClient(string ip = "127.0.0.1", int port = 47110)
        {
            _client = new AnTcpClient(ip, port);

            // Initialize with server defaults
            _state = ClientState.Normal;
            SetAreaCosts(ground: 1f, road: 0.75f, water: 1.6f, badLiquid: 4f);
        }

        /// <summary>
        /// Connect to the server. Returns true on success.
        /// </summary>
        public bool TryConnect()
        {
            if (_client.IsConnected) return true;

            try
            {
                _client.Connect();

                if (_client.IsConnected)
                {
                    _wasConnected = true;
                    Connected?.Invoke();
                    return true;
                }

                return false;
            }
            catch
            {
                return false;
            }
        }

        // ── Pathfinding ─────────────────────────────────────────────────

        /// <summary>
        /// Find a straight path from start to end. Returns null on failure.
        /// </summary>
        public Vector3[]? GetPath(int mapId, Vector3 start, Vector3 end, PathFlags flags = PathFlags.None)
        {
            return SendPathRequest(MessageType.Path, mapId, start, end, flags);
        }

        /// <summary>
        /// Find a path with randomized waypoints (human-like movement). Returns null on failure.
        /// </summary>
        public Vector3[]? GetRandomPath(int mapId, Vector3 start, Vector3 end, PathFlags flags = PathFlags.None)
        {
            return SendPathRequest(MessageType.RandomPath, mapId, start, end, flags);
        }

        /// <summary>
        /// Move along the navmesh surface by a small delta. Useful for preventing
        /// falling off edges during incremental movement.
        /// Returns the clamped target position, or a zero vector on failure.
        /// </summary>
        public Vector3 MoveAlongSurface(int mapId, Vector3 start, Vector3 end)
        {
            lock (_lock)
            {
                var request = new MoveRequestData { MapId = mapId, Start = start, End = end };
                return SendWithReconnect(
                    () => _client.Send((byte)MessageType.MoveAlongSurface, request).As<Vector3>(),
                    default
                );
            }
        }

        /// <summary>
        /// Cast a movement ray to test for obstacles between start and end.
        /// Returns true if the ray is clear (no wall hit). On hit, hitPoint is
        /// set to the end position that was tested.
        /// </summary>
        public bool CastRay(int mapId, Vector3 start, Vector3 end, out Vector3 hitPoint)
        {
            lock (_lock)
            {
                var request = new CastRayData { MapId = mapId, Start = start, End = end };
                var result = SendWithReconnect(
                    () => _client.Send((byte)MessageType.CastRay, request).As<Vector3>(),
                    default
                );

                hitPoint = result;
                return !result.IsZero;
            }
        }

        /// <summary>
        /// Get a random point anywhere on the navmesh for the given map.
        /// </summary>
        public Vector3 GetRandomPoint(int mapId)
        {
            lock (_lock)
            {
                return SendWithReconnect(
                    () => _client.Send((byte)MessageType.RandomPoint, mapId).As<Vector3>(),
                    default
                );
            }
        }

        /// <summary>
        /// Get a random point on the navmesh within a radius of the given center.
        /// </summary>
        public Vector3 GetRandomPointAround(int mapId, Vector3 center, float radius)
        {
            lock (_lock)
            {
                var request = new RandomPointAroundData { MapId = mapId, Start = center, Radius = radius };
                return SendWithReconnect(
                    () => _client.Send((byte)MessageType.RandomPointAround, request).As<Vector3>(),
                    default
                );
            }
        }

        // ── Filter configuration ────────────────────────────────────────

        /// <summary>
        /// Set the client state (faction). Call <see cref="ApplyFilter"/> to send to server.
        /// </summary>
        public void SetClientState(ClientState state)
        {
            _state = state;
            IsFilterDirty = true;
        }

        /// <summary>
        /// Set the cost for a single area ID (1-27). Call <see cref="ApplyFilter"/> to send.
        /// </summary>
        public void SetAreaCost(byte areaId, float cost)
        {
            if (areaId >= 1 && areaId <= 27)
            {
                _areaCosts[areaId] = cost;
                IsFilterDirty = true;
            }
        }

        /// <summary>
        /// Set costs for all 27 areas at once using category-based costs.
        /// Faction multipliers scale alliance/horde area variants.
        /// Call <see cref="ApplyFilter"/> to send to the server.
        /// </summary>
        /// <param name="ground">Base cost for ground, city, WMO, doodad surfaces.</param>
        /// <param name="road">Cost for road surfaces (typically &lt; 1 to prefer roads).</param>
        /// <param name="water">Cost for water and ocean surfaces.</param>
        /// <param name="badLiquid">Cost for lava and slime surfaces.</param>
        /// <param name="allyMult">Multiplier for Alliance faction areas (raise to avoid).</param>
        /// <param name="hordeMult">Multiplier for Horde faction areas (raise to avoid).</param>
        public void SetAreaCosts(float ground, float road, float water, float badLiquid,
                                 float allyMult = 1f, float hordeMult = 1f)
        {
            // Ground
            _areaCosts[AnpArea.TERRAIN_GROUND] = ground;
            _areaCosts[AnpArea.ALLIANCE_TERRAIN_GROUND] = ground * allyMult;
            _areaCosts[AnpArea.HORDE_TERRAIN_GROUND] = ground * hordeMult;

            // Road
            _areaCosts[AnpArea.TERRAIN_ROAD] = road;
            _areaCosts[AnpArea.ALLIANCE_TERRAIN_ROAD] = road * allyMult;
            _areaCosts[AnpArea.HORDE_TERRAIN_ROAD] = road * hordeMult;

            // City
            _areaCosts[AnpArea.TERRAIN_CITY] = ground;
            _areaCosts[AnpArea.ALLIANCE_TERRAIN_CITY] = ground * allyMult;
            _areaCosts[AnpArea.HORDE_TERRAIN_CITY] = ground * hordeMult;

            // WMO
            _areaCosts[AnpArea.WMO] = ground;
            _areaCosts[AnpArea.ALLIANCE_WMO] = ground * allyMult;
            _areaCosts[AnpArea.HORDE_WMO] = ground * hordeMult;

            // Doodad
            _areaCosts[AnpArea.DOODAD] = ground;
            _areaCosts[AnpArea.ALLIANCE_DOODAD] = ground * allyMult;
            _areaCosts[AnpArea.HORDE_DOODAD] = ground * hordeMult;

            // Water
            _areaCosts[AnpArea.LIQUID_WATER] = water;
            _areaCosts[AnpArea.ALLIANCE_LIQUID_WATER] = water * allyMult;
            _areaCosts[AnpArea.HORDE_LIQUID_WATER] = water * hordeMult;

            // Ocean
            _areaCosts[AnpArea.LIQUID_OCEAN] = water;
            _areaCosts[AnpArea.ALLIANCE_LIQUID_OCEAN] = water * allyMult;
            _areaCosts[AnpArea.HORDE_LIQUID_OCEAN] = water * hordeMult;

            // Lava (always expensive, faction doesn't matter)
            _areaCosts[AnpArea.LIQUID_LAVA] = badLiquid;
            _areaCosts[AnpArea.ALLIANCE_LIQUID_LAVA] = badLiquid;
            _areaCosts[AnpArea.HORDE_LIQUID_LAVA] = badLiquid;

            // Slime
            _areaCosts[AnpArea.LIQUID_SLIME] = badLiquid;
            _areaCosts[AnpArea.ALLIANCE_LIQUID_SLIME] = badLiquid;
            _areaCosts[AnpArea.HORDE_LIQUID_SLIME] = badLiquid;

            IsFilterDirty = true;
        }

        /// <summary>
        /// Send the current filter configuration to the server.
        /// Returns true on success. Call this after changing state or area costs.
        /// </summary>
        public bool ApplyFilter()
        {
            lock (_lock)
            {
                return SendWithReconnect(() =>
                {
                    // Wire format: [state(1)+pad(3)][count(4)][entries: {areaId(1)+pad(3)+cost(4)} × N]
                    const int entryCount = 27;
                    const int headerSize = 8;
                    const int entrySize = 8;
                    byte[] buffer = new byte[headerSize + entryCount * entrySize];

                    buffer[0] = (byte)_state;
                    BitConverter.GetBytes(entryCount).CopyTo(buffer, 4);

                    for (int i = 0; i < entryCount; i++)
                    {
                        int off = headerSize + i * entrySize;
                        byte areaId = (byte)(i + 1);
                        buffer[off] = areaId;
                        BitConverter.GetBytes(_areaCosts[areaId]).CopyTo(buffer, off + 4);
                    }

                    bool result = _client.SendBytes((byte)MessageType.ConfigureFilter, buffer).As<bool>();
                    if (result) IsFilterDirty = false;
                    return result;
                }, false);
            }
        }

        // ── Internals ───────────────────────────────────────────────────

        private Vector3[]? SendPathRequest(MessageType type, int mapId, Vector3 start, Vector3 end, PathFlags flags)
        {
            lock (_lock)
            {
                return SendWithReconnect(() =>
                {
                    var request = new PathRequestData
                    {
                        MapId = mapId,
                        Flags = (int)flags,
                        Start = start,
                        End = end,
                    };

                    var points = _client.Send((byte)type, request).AsArray<Vector3>();
                    if (points.Length == 0) return null;

                    // Server returns a single zero vector on failure
                    if (points.Length == 1 && points[0].IsZero) return null;

                    return points;
                }, null);
            }
        }

        /// <summary>
        /// Execute a server call. On network failure, attempt auto-reconnect
        /// and retry once. Returns <paramref name="fallback"/> on total failure.
        /// </summary>
        private T SendWithReconnect<T>(Func<T> action, T fallback)
        {
            try
            {
                return action();
            }
            catch (Exception ex) when (IsNetworkError(ex))
            {
                if (!AutoReconnect)
                {
                    OnDisconnected();
                    return fallback;
                }

                if (!TryAutoReconnect())
                    return fallback;

                // Retry once after successful reconnect
                try
                {
                    return action();
                }
                catch
                {
                    return fallback;
                }
            }
            catch
            {
                return fallback;
            }
        }

        private bool TryAutoReconnect()
        {
            OnDisconnected();

            int maxAttempts = MaxReconnectAttempts;
            int attempt = 0;

            while (maxAttempts == 0 || attempt < maxAttempts)
            {
                attempt++;
                Reconnecting?.Invoke(attempt);

                Thread.Sleep(ReconnectDelayMs);

                try
                {
                    if (_client.TryReconnect())
                    {
                        _wasConnected = true;
                        Connected?.Invoke();

                        // Re-apply filter on reconnect so the server knows our faction/costs
                        if (IsFilterDirty || true) // always re-apply after reconnect
                        {
                            try { ApplyFilterInternal(); } catch { }
                        }

                        return true;
                    }
                }
                catch { }
            }

            return false;
        }

        /// <summary>
        /// Internal filter apply that doesn't go through the lock or reconnect wrapper
        /// (called from within TryAutoReconnect which already holds the lock).
        /// </summary>
        private void ApplyFilterInternal()
        {
            const int entryCount = 27;
            const int headerSize = 8;
            const int entrySize = 8;
            byte[] buffer = new byte[headerSize + entryCount * entrySize];

            buffer[0] = (byte)_state;
            BitConverter.GetBytes(entryCount).CopyTo(buffer, 4);

            for (int i = 0; i < entryCount; i++)
            {
                int off = headerSize + i * entrySize;
                byte areaId = (byte)(i + 1);
                buffer[off] = areaId;
                BitConverter.GetBytes(_areaCosts[areaId]).CopyTo(buffer, off + 4);
            }

            if (_client.SendBytes((byte)MessageType.ConfigureFilter, buffer).As<bool>())
                IsFilterDirty = false;
        }

        private void OnDisconnected()
        {
            if (_wasConnected)
            {
                _wasConnected = false;
                Disconnected?.Invoke();
            }
        }

        private static bool IsNetworkError(Exception ex)
        {
            return ex is IOException
                || ex is SocketException
                || ex is ObjectDisposedException
                || (ex.InnerException != null && IsNetworkError(ex.InnerException));
        }

        public void Dispose()
        {
            try { if (_client.IsConnected) _client.Disconnect(); } catch { }
        }
    }
}
