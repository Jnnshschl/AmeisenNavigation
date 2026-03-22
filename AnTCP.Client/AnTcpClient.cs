using AnTCP.Client.Objects;
using System;
using System.Buffers.Binary;
using System.IO;
using System.Net.Sockets;
using System.Runtime.CompilerServices;

namespace AnTCP.Client
{
    public unsafe class AnTcpClient(string ip, int port) : IDisposable
    {
        public string Ip { get; } = ip;

        public bool IsConnected => Client != null && Client.Connected;

        public int Port { get; } = port;

        private TcpClient Client { get; set; }

        private NetworkStream Stream { get; set; }

        // Reusable buffers - grow as needed, never shrink.
        // Safe because each AnTcpClient instance is used from one thread at a time
        // (AmeisenNavClient wraps calls in a lock).
        private byte[] _sendBuf = new byte[256];
        private byte[] _recvBuf = new byte[4096];

        /// <summary>
        /// Connect to the server.
        /// </summary>
        public void Connect()
        {
            Client = new(Ip, Port);
            Client.NoDelay = true;
            Stream = Client.GetStream();
        }

        /// <summary>
        /// Disconnect from the server.
        /// </summary>
        public void Disconnect()
        {
            Stream?.Close();
            Client?.Close();
        }

        /// <summary>
        /// Dispose the current connection and attempt a fresh connect.
        /// Returns true if the new connection succeeded.
        /// </summary>
        public bool TryReconnect()
        {
            try
            {
                Stream?.Dispose();
                Client?.Dispose();
            }
            catch { }

            try
            {
                Client = new(Ip, Port);
                Client.NoDelay = true;
                Stream = Client.GetStream();
                return Client.Connected;
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
        /// <returns>Server response</returns>
        public AnTcpResponse Send<T>(byte type, T data) where T : unmanaged
        {
            int dataSize = sizeof(T);
            int payloadSize = 1 + dataSize;
            int totalSize = 4 + payloadSize;

            EnsureSendBuffer(totalSize);

            BinaryPrimitives.WriteInt32LittleEndian(_sendBuf, payloadSize);
            _sendBuf[4] = type;
            new ReadOnlySpan<byte>(&data, dataSize).CopyTo(_sendBuf.AsSpan(5));

            Stream.Write(_sendBuf, 0, totalSize);
            return ReadResponse();
        }

        /// <summary>
        /// Send a byte array to the server.
        /// </summary>
        /// <param name="type">Message type</param>
        /// <param name="data">Data to send</param>
        /// <returns>Server response</returns>
        public AnTcpResponse SendBytes(byte type, ReadOnlySpan<byte> data)
        {
            int payloadSize = 1 + data.Length;
            int totalSize = 4 + payloadSize;

            EnsureSendBuffer(totalSize);

            BinaryPrimitives.WriteInt32LittleEndian(_sendBuf, payloadSize);
            _sendBuf[4] = type;
            data.CopyTo(_sendBuf.AsSpan(5));

            Stream.Write(_sendBuf, 0, totalSize);
            return ReadResponse();
        }

        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        private AnTcpResponse ReadResponse()
        {
            ReadExact(4);
            int responseSize = BinaryPrimitives.ReadInt32LittleEndian(_recvBuf);

            EnsureRecvBuffer(responseSize);
            ReadExact(responseSize);

            return new AnTcpResponse(_recvBuf, responseSize);
        }

        private void ReadExact(int count)
        {
            int offset = 0;

            while (offset < count)
            {
                int read = Stream.Read(_recvBuf, offset, count - offset);

                if (read == 0)
                    throw new IOException("Server closed the connection.");

                offset += read;
            }
        }

        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        private void EnsureSendBuffer(int required)
        {
            if (_sendBuf.Length < required)
                _sendBuf = new byte[required];
        }

        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        private void EnsureRecvBuffer(int required)
        {
            if (_recvBuf.Length < required)
                _recvBuf = new byte[required];
        }

        public void Dispose()
        {
            Stream?.Dispose();
            Client?.Dispose();
        }
    }
}
