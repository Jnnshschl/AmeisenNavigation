using System;
using System.Runtime.InteropServices;

namespace AnTCP.Client.Objects
{
    public readonly unsafe ref struct AnTcpResponse
    {
        private readonly Span<byte> _data;

        internal AnTcpResponse(byte[] buffer, int length)
        {
            Type = buffer[0];
            _data = buffer.AsSpan(1, length - 1);
        }

        /// <summary>
        /// Raw response data (excluding the type byte).
        /// Backed by a reusable buffer - only valid until the next Send call.
        /// </summary>
        public Span<byte> Data => _data;

        /// <summary>
        /// Length of the data (excluding the type byte).
        /// </summary>
        public int Length => _data.Length;

        /// <summary>
        /// Type of the response.
        /// </summary>
        public byte Type { get; }

        /// <summary>
        /// Read the data as a single unmanaged value. Zero allocations.
        /// </summary>
        public T As<T>() where T : unmanaged
            => MemoryMarshal.Read<T>(_data);

        /// <summary>
        /// Copy the data into a new array of unmanaged values.
        /// One allocation for the result array.
        /// </summary>
        public T[] AsArray<T>() where T : unmanaged
            => MemoryMarshal.Cast<byte, T>(_data).ToArray();

        /// <summary>
        /// View the data as a span of unmanaged values. Zero allocations.
        /// Only valid until the next Send call on the same client.
        /// </summary>
        public ReadOnlySpan<T> AsSpan<T>() where T : unmanaged
            => MemoryMarshal.Cast<byte, T>(_data);
    }
}
