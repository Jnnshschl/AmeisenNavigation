using System;
using System.Runtime.CompilerServices;

namespace AmeisenNavigation.Server
{
    public static class Utils
    {
        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        public unsafe static T FromBytes<T>(byte[] bytes) where T : unmanaged
        {
            if (bytes == null || bytes.Length == 0 || bytes.Length < sizeof(T)) return default;

            fixed (byte* pBytes = bytes)
            {
                return *(T*)pBytes;
            }
        }

        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        public unsafe static byte[] ToBytes<T>(T* obj, int size) where T : unmanaged
        {
            if ((int)obj == 0) return null;

            byte[] bytes = new byte[size];

            fixed (byte* pBytes = bytes)
                Buffer.MemoryCopy(obj, pBytes, size, size);

            return bytes;
        }
    }
}