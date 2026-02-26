using System;
using System.Runtime.InteropServices;

namespace AmeisenNavigation.Tester.Services
{
    public static class StormLib
    {
        private const string DllName = "StormLib.dll";

        public const uint MPQ_OPEN_READ_ONLY = 0x00000100;

        // StormLib uses C++ bool (1 byte), not Windows BOOL (4 bytes).
        // Must use [return: MarshalAs(UnmanagedType.I1)] to marshal correctly.

        [DllImport(DllName, CallingConvention = CallingConvention.Winapi, CharSet = CharSet.Unicode, SetLastError = true)]
        [return: MarshalAs(UnmanagedType.I1)]
        public static extern bool SFileOpenArchive(
            [MarshalAs(UnmanagedType.LPTStr)] string szMpqName,
            uint dwPriority,
            uint dwFlags,
            out IntPtr phMpq);

        [DllImport(DllName, CallingConvention = CallingConvention.Winapi, SetLastError = true)]
        [return: MarshalAs(UnmanagedType.I1)]
        public static extern bool SFileCloseArchive(IntPtr hMpq);

        [DllImport(DllName, CallingConvention = CallingConvention.Winapi, CharSet = CharSet.Ansi, SetLastError = true)]
        [return: MarshalAs(UnmanagedType.I1)]
        public static extern bool SFileHasFile(IntPtr hMpq, string szFileName);

        [DllImport(DllName, CallingConvention = CallingConvention.Winapi, CharSet = CharSet.Ansi, SetLastError = true)]
        [return: MarshalAs(UnmanagedType.I1)]
        public static extern bool SFileOpenFileEx(
            IntPtr hMpq,
            string szFileName,
            uint dwSearchScope,
            out IntPtr phFile);

        [DllImport(DllName, CallingConvention = CallingConvention.Winapi, SetLastError = true)]
        public static extern uint SFileGetFileSize(IntPtr hFile, IntPtr pdwFileSizeHigh);

        [DllImport(DllName, CallingConvention = CallingConvention.Winapi, SetLastError = true)]
        [return: MarshalAs(UnmanagedType.I1)]
        public static extern bool SFileReadFile(
            IntPtr hFile,
            byte[] lpBuffer,
            uint dwToRead,
            out uint pdwRead,
            IntPtr lpOverlapped);

        [DllImport(DllName, CallingConvention = CallingConvention.Winapi, SetLastError = true)]
        [return: MarshalAs(UnmanagedType.I1)]
        public static extern bool SFileCloseFile(IntPtr hFile);
    }
}
