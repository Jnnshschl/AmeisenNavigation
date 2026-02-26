using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.Text;

namespace AmeisenNavigation.Tester.Services
{
    public sealed class MpqArchiveSet : IDisposable
    {
        private readonly List<IntPtr> _handles = [];

        public bool IsLoaded => _handles.Count > 0;
        public int ArchiveCount => _handles.Count;

        public bool Load(string wowDataDir)
        {
            Dispose();

            var mpqFiles = Directory.GetFiles(wowDataDir, "*.mpq", SearchOption.AllDirectories)
                .OrderByDescending(f => f, StringComparer.OrdinalIgnoreCase)
                .ToList();

            foreach (var mpqPath in mpqFiles)
            {
                if (StormLib.SFileOpenArchive(mpqPath, 0, StormLib.MPQ_OPEN_READ_ONLY, out var handle))
                {
                    _handles.Add(handle);
                }
            }

            return _handles.Count > 0;
        }

        public bool HasFile(string fileName)
        {
            foreach (var mpq in _handles)
            {
                if (StormLib.SFileHasFile(mpq, fileName))
                    return true;
            }
            return false;
        }

        public byte[]? ReadFile(string fileName)
        {
            foreach (var mpq in _handles)
            {
                if (!StormLib.SFileHasFile(mpq, fileName))
                    continue;

                if (!StormLib.SFileOpenFileEx(mpq, fileName, 0, out var fileHandle))
                    continue;

                try
                {
                    uint size = StormLib.SFileGetFileSize(fileHandle, IntPtr.Zero);
                    if (size == 0 || size == 0xFFFFFFFF)
                        continue;

                    var buffer = new byte[size];
                    if (StormLib.SFileReadFile(fileHandle, buffer, size, out uint read, IntPtr.Zero) && read == size)
                        return buffer;
                }
                finally
                {
                    StormLib.SFileCloseFile(fileHandle);
                }
            }

            return null;
        }

        /// <summary>
        /// Detailed diagnostic version of ReadFile that reports exactly which step fails.
        /// </summary>
        public string DiagnoseReadFile(string fileName)
        {
            var sb = new StringBuilder();
            sb.AppendLine($"DiagnoseReadFile(\"{fileName}\"):");

            for (int i = 0; i < _handles.Count; i++)
            {
                var mpq = _handles[i];
                bool has = StormLib.SFileHasFile(mpq, fileName);
                if (!has) continue;

                sb.AppendLine($"  Archive[{i}] handle=0x{mpq:X}: HasFile=True");

                bool opened = StormLib.SFileOpenFileEx(mpq, fileName, 0, out var fileHandle);
                int openErr = Marshal.GetLastWin32Error();
                sb.AppendLine($"    OpenFileEx: {opened} (err={openErr}, handle=0x{fileHandle:X})");

                if (!opened) continue;

                try
                {
                    uint size = StormLib.SFileGetFileSize(fileHandle, IntPtr.Zero);
                    int sizeErr = Marshal.GetLastWin32Error();
                    sb.AppendLine($"    GetFileSize: {size} (err={sizeErr})");

                    if (size == 0 || size == 0xFFFFFFFF) continue;

                    var buffer = new byte[size];
                    bool readOk = StormLib.SFileReadFile(fileHandle, buffer, size, out uint read, IntPtr.Zero);
                    int readErr = Marshal.GetLastWin32Error();
                    sb.AppendLine($"    ReadFile: {readOk}, read={read}/{size} (err={readErr})");

                    if (readOk && read > 0)
                    {
                        // Show first 8 bytes as hex
                        int preview = (int)Math.Min(read, 8);
                        sb.Append("    First bytes: ");
                        for (int b = 0; b < preview; b++)
                            sb.Append($"{buffer[b]:X2} ");
                        sb.AppendLine();
                    }
                }
                finally
                {
                    StormLib.SFileCloseFile(fileHandle);
                }
            }

            if (sb.ToString().Split('\n').Length <= 2)
                sb.AppendLine("  (no archive contains this file)");

            return sb.ToString();
        }

        public void Dispose()
        {
            foreach (var handle in _handles)
                StormLib.SFileCloseArchive(handle);

            _handles.Clear();
        }
    }
}
