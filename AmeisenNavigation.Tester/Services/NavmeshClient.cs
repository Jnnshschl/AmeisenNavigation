using AnTCP.Client;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Runtime.InteropServices;
using System.Threading;
using System.Threading.Tasks;

namespace AmeisenNavigation.Tester.Services
{
    public enum MessageType : byte
    {
        PATH,
        MOVE_ALONG_SURFACE,
        RANDOM_POINT,
        RANDOM_POINT_AROUND,
        CAST_RAY,
        RANDOM_PATH,
        EXPLORE_POLY,
        CONFIGURE_FILTER,
    }

    public enum PathType
    {
        STRAIGHT,
        RANDOM,
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct FilterConfig
    {
        public char State;
        public int Count = 3;
        public char GroundArea = (char)13;
        public float GroundCost;
        public char WaterArea = (char)10;
        public float WaterAreaCost;
        public char BadLiquidArea = (char)1;
        public float BadLiquidCost;

        public FilterConfig() { }
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct MoveRequestData
    {
        public int MapId;
        public Vector3 Start;
        public Vector3 End;
    }

    public sealed class NavmeshClient : IDisposable
    {
        private readonly AnTcpClient _client;
        private readonly SemaphoreSlim _lock = new(1, 1);

        public bool IsConnected => _client.IsConnected;

        public NavmeshClient(string ip = "127.0.0.1", int port = 47110)
        {
            _client = new(ip, port);
        }

        public async Task<bool> TryConnectAsync()
        {
            if (_client.IsConnected) return true;

            return await Task.Run(() =>
            {
                try
                {
                    _client.Connect();
                    return _client.IsConnected;
                }
                catch
                {
                    return false;
                }
            });
        }

        public async Task<List<Vector3>> GetPathAsync(MessageType msgType, int mapId, Vector3 start, Vector3 end, int flags)
        {
            if (!IsConnected) return [];

            await _lock.WaitAsync();
            try
            {
                return await Task.Run(() =>
                {
                    try
                    {
                        return _client.Send((byte)msgType, (mapId, flags, start, end)).AsArray<Vector3>().ToList();
                    }
                    catch
                    {
                        return new List<Vector3>();
                    }
                });
            }
            finally
            {
                _lock.Release();
            }
        }

        public async Task<Vector3> GetRandomPointAsync(int mapId)
        {
            if (!IsConnected) return new();

            await _lock.WaitAsync();
            try
            {
                return await Task.Run(() =>
                {
                    try
                    {
                        return _client.Send((byte)MessageType.RANDOM_POINT, mapId).As<Vector3>();
                    }
                    catch
                    {
                        return new Vector3();
                    }
                });
            }
            finally
            {
                _lock.Release();
            }
        }

        public async Task<Vector3> MoveAlongSurfaceAsync(int mapId, Vector3 start, Vector3 end)
        {
            if (!IsConnected) return new();

            await _lock.WaitAsync();
            try
            {
                return await Task.Run(() =>
                {
                    try
                    {
                        var request = new MoveRequestData { MapId = mapId, Start = start, End = end };
                        return _client.Send((byte)MessageType.MOVE_ALONG_SURFACE, request).As<Vector3>();
                    }
                    catch
                    {
                        return new Vector3();
                    }
                });
            }
            finally
            {
                _lock.Release();
            }
        }

        public async Task<bool> ConfigureFilterAsync(FilterConfig config)
        {
            if (!IsConnected) return false;

            await _lock.WaitAsync();
            try
            {
                return await Task.Run(() =>
                {
                    try
                    {
                        return _client.Send((byte)MessageType.CONFIGURE_FILTER, config).As<bool>();
                    }
                    catch
                    {
                        return false;
                    }
                });
            }
            finally
            {
                _lock.Release();
            }
        }

        public void Dispose()
        {
            try { if (_client.IsConnected) _client.Disconnect(); } catch { }
            _lock.Dispose();
        }
    }
}
