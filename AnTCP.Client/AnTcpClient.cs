using AnTCP.Client.Objects;
using System;
using System.Buffers.Binary;
using System.IO;
using System.Net.Sockets;
using System.Runtime.CompilerServices;

namespace AnTCP.Client
{
    /// <summary>
    /// Client for AnTCP servers. Every message (both directions) is framed as
    /// <c>int32 size (little endian, type + payload) | uint8 type | payload</c>.
    /// Not thread-safe: use one instance per thread or synchronize externally.
    /// </summary>
    public unsafe class AnTcpClient(string ip, int port) : IDisposable
    {
        /// <summary>Largest response accepted from the server (sanity limit against corrupt streams).</summary>
        public const int MaxResponseSize = 64 * 1024 * 1024;

        public string Ip { get; } = ip;

        public int Port { get; } = port;

        public bool IsConnected => Client != null && Client.Connected;

        /// <summary>
        /// Send/receive timeout in milliseconds, 0 = infinite. A timeout surfaces as <see cref="IOException"/>.
        /// Default: 30 seconds (the first request for a map may have to load it from disk).
        /// </summary>
        public int TimeoutMs { get; set; } = 30000;

        private TcpClient? Client { get; set; }

        private NetworkStream? Stream { get; set; }

        // Reusable buffers - grow as needed, never shrink.
        private byte[] _sendBuf = new byte[256];
        private byte[] _recvBuf = new byte[4096];

        /// <summary>
        /// Connect to the server (closes an existing connection first).
        /// </summary>
        public void Connect()
        {
            CloseConnection();

            var client = new TcpClient { NoDelay = true };

            try
            {
                client.Connect(Ip, Port);
                client.ReceiveTimeout = TimeoutMs;
                client.SendTimeout = TimeoutMs;
                Stream = client.GetStream();
                Client = client;
            }
            catch
            {
                client.Dispose();
                throw;
            }
        }

        /// <summary>
        /// Disconnect from the server.
        /// </summary>
        public void Disconnect() => CloseConnection();

        /// <summary>
        /// Dispose the current connection and attempt a fresh connect.
        /// Returns true if the new connection succeeded.
        /// </summary>
        public bool TryReconnect()
        {
            try
            {
                Connect();
                return IsConnected;
            }
            catch
            {
                return false;
            }
        }

        /// <summary>
        /// Send data to the server, data can be any unmanaged type.
        /// </summary>
        /// <typeparam name="T">Unmanaged type of the data</typeparam>
        /// <param name="type">Message type</param>
        /// <param name="data">Data to send</param>
        /// <returns>Server response (valid until the next call)</returns>
        public AnTcpResponse Send<T>(byte type, T data) where T : unmanaged
        {
            return SendBytes(type, new ReadOnlySpan<byte>(&data, sizeof(T)));
        }

        /// <summary>
        /// Send a byte array to the server.
        /// </summary>
        /// <param name="type">Message type</param>
        /// <param name="data">Data to send</param>
        /// <returns>Server response (valid until the next call)</returns>
        public AnTcpResponse SendBytes(byte type, ReadOnlySpan<byte> data)
        {
            NetworkStream stream = Stream ?? throw new IOException("Not connected.");

            int payloadSize = 1 + data.Length;
            int totalSize = 4 + payloadSize;

            EnsureSendBuffer(totalSize);

            BinaryPrimitives.WriteInt32LittleEndian(_sendBuf, payloadSize);
            _sendBuf[4] = type;
            data.CopyTo(_sendBuf.AsSpan(5));

            stream.Write(_sendBuf, 0, totalSize);
            return ReadResponse(stream, type);
        }

        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        private AnTcpResponse ReadResponse(NetworkStream stream, byte expectedType)
        {
            ReadExact(stream, 4);
            int responseSize = BinaryPrimitives.ReadInt32LittleEndian(_recvBuf);

            if (responseSize < 1 || responseSize > MaxResponseSize)
            {
                CloseConnection();
                throw new IOException($"Invalid response size {responseSize}, the stream is out of sync.");
            }

            EnsureRecvBuffer(responseSize);
            ReadExact(stream, responseSize);

            if (_recvBuf[0] != expectedType)
            {
                CloseConnection();
                throw new IOException($"Response type {_recvBuf[0]} does not match request type {expectedType}.");
            }

            return new AnTcpResponse(_recvBuf, responseSize);
        }

        private void ReadExact(NetworkStream stream, int count)
        {
            int offset = 0;

            while (offset < count)
            {
                int read = stream.Read(_recvBuf, offset, count - offset);

                if (read == 0)
                    throw new IOException("Server closed the connection.");

                offset += read;
            }
        }

        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        private void EnsureSendBuffer(int required)
        {
            if (_sendBuf.Length < required)
                _sendBuf = new byte[Math.Max(required, _sendBuf.Length * 2)];
        }

        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        private void EnsureRecvBuffer(int required)
        {
            if (_recvBuf.Length < required)
                _recvBuf = new byte[Math.Max(required, _recvBuf.Length * 2)];
        }

        private void CloseConnection()
        {
            try { Stream?.Dispose(); } catch { }
            try { Client?.Dispose(); } catch { }
            Stream = null;
            Client = null;
        }

        public void Dispose()
        {
            CloseConnection();
            GC.SuppressFinalize(this);
        }
    }
}
