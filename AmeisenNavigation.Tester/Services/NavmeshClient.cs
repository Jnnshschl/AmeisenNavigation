using AmeisenNavigation.Client;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading.Tasks;

namespace AmeisenNavigation.Tester.Services
{
    public enum PathType
    {
        STRAIGHT,
        RANDOM,
    }

    public sealed class NavmeshClient : IDisposable
    {
        private readonly AmeisenNavClient _nav;

        public bool IsConnected => _nav.IsConnected;

        public NavmeshClient(string ip = "127.0.0.1", int port = 47110)
        {
            _nav = new AmeisenNavClient(ip, port);
        }

        public Task<bool> TryConnectAsync()
        {
            return Task.Run(() => _nav.TryConnect());
        }

        public Task<List<Vector3>> GetPathAsync(PathType pathType, int mapId, Vector3 start, Vector3 end, PathFlags flags)
        {
            return Task.Run(() =>
            {
                var path = pathType == PathType.RANDOM
                    ? _nav.GetRandomPath(mapId, start, end, flags)
                    : _nav.GetPath(mapId, start, end, flags);
                return path?.ToList() ?? [];
            });
        }

        public Task<Vector3> GetRandomPointAsync(int mapId)
        {
            return Task.Run(() => _nav.GetRandomPoint(mapId));
        }

        public Task<Vector3> MoveAlongSurfaceAsync(int mapId, Vector3 start, Vector3 end)
        {
            return Task.Run(() => _nav.MoveAlongSurface(mapId, start, end));
        }

        public Task<bool> ApplyFilterAsync(byte state, float ground, float road, float water, float badLiquid, float allyMult, float hordeMult)
        {
            return Task.Run(() =>
            {
                _nav.SetClientState((ClientState)state);
                _nav.SetAreaCosts(ground, road, water, badLiquid, allyMult, hordeMult);
                return _nav.ApplyFilter();
            });
        }

        public void Dispose()
        {
            _nav.Dispose();
        }
    }
}
