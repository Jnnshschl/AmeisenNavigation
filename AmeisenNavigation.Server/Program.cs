using AmeisenNavigation.Server.Objects;
using AmeisenNavigation.Server.Objects.Enums;
using AmeisenNavigation.Server.Transformations;
using AmeisenNavigationWrapper;
using Newtonsoft.Json;
using System;
using System.Collections.Concurrent;
using System.Diagnostics;
using System.IO;
using System.Net;
using System.Net.Sockets;
using System.Runtime.CompilerServices;
using System.Text;
using System.Threading;

namespace AmeisenNavigation.Server
{
    internal static class Program
    {
        private static readonly object querylock = new object();
        private static int clientCount = 0;

        private static AmeisenNav AmeisenNav { get; set; }

        private static Thread LoggingThread { get; set; }

        private static ConcurrentQueue<LogEntry> LogQueue { get; set; }

        private static Settings Settings { get; set; }

        private static string SettingsPath { get; } = AppDomain.CurrentDomain.BaseDirectory + "config.json";

        private static bool StopServer { get; set; }

        private static TcpListener TcpListener { get; set; }

        public static string BuildLog(string s, LogLevel logLevel)
            => $"{$"[{logLevel}]",9} >> {s}";

        public static string ColoredPrint(string s, ConsoleColor color, string uncoloredOutput = "", LogLevel logLevel = LogLevel.INFO)
        {
            string logString = BuildLog(s, logLevel);

            Console.ForegroundColor = color;
            Console.Write(logString);
            Console.ResetColor();
            Console.WriteLine(uncoloredOutput);

            return $"{logString}{uncoloredOutput}";
        }

        public static void LogThreadRoutine()
        {
            while (!StopServer || LogQueue.Count > 0)
            {
                StringBuilder sb = new StringBuilder();

                while (LogQueue.TryDequeue(out LogEntry logEntry))
                {
                    if (logEntry.LogLevel >= Settings.LogLevel)
                    {
                        string logString = ColoredPrint(logEntry.ColoredPart, logEntry.Color, logEntry.UncoloredPart, logEntry.LogLevel);

                        if (Settings.LogToFile)
                        {
                            sb.AppendLine(logString);
                        }
                    }
                }

                try
                {
                    File.WriteAllText(Settings.LogFilePath, sb.ToString());
                }
                catch { }

                Thread.Sleep(100);
            }
        }

        public static void Main()
        {
            PrintHeader();

            LogQueue = new ConcurrentQueue<LogEntry>();
            Settings = LoadConfigFile();

            LogSetup();

            UpdateConnectedClientCount();

            if (Settings == null)
            {
                Console.ReadKey();
            }
            else if (!Directory.Exists(Settings.MmapsFolder))
            {
                LogQueue.Enqueue(new LogEntry("MMAP folder missing, edit folder in config.json...", ConsoleColor.Red, string.Empty, LogLevel.ERROR));
                Console.ReadKey();
            }
            else
            {
                Settings.MmapsFolder = Settings.MmapsFolder.Replace('/', '\\');
                AmeisenNav = new AmeisenNav(Settings.MmapsFolder, Settings.MaxPolyPathCount, Settings.MaxPointPathCount);

                if (Settings.PreloadMaps.Length > 0)
                {
                    PreloadMaps();
                }

                TcpListener = new TcpListener(IPAddress.Parse(Settings.IpAddress), Settings.Port);
                TcpListener.Start();

                LogQueue.Enqueue(new LogEntry($"Listening on {Settings.IpAddress}:{Settings.Port} press Ctrl + C to exit...", ConsoleColor.Green, string.Empty, LogLevel.MASTER));

                TcpEnterServerLoop();

                AmeisenNav.Dispose();
            }
        }

        public static void TcpEnterServerLoop()
        {
            while (!StopServer)
            {
                try
                {
                    TcpClient newClient = TcpListener.AcceptTcpClient();
                    Thread userThread = new Thread(new ThreadStart(() => TcpHandleClient(newClient)));
                    userThread.Start();
                }
                catch (Exception e)
                {
                    if (!StopServer)
                    {
                        string errorMsg = $"{e.GetType()} occured while at the TcpListener\n";
                        LogQueue.Enqueue(new LogEntry(errorMsg, ConsoleColor.Red, e.ToString(), LogLevel.ERROR));
                    }
                }
            }
        }

        public unsafe static void TcpHandleClient(TcpClient client)
        {
            LogQueue.Enqueue(new LogEntry($"Client connected: ", ConsoleColor.Cyan, client.Client.RemoteEndPoint.ToString()));

            using Stream stream = client.GetStream();
            using BinaryReader binaryReader = new BinaryReader(stream);
            using BinaryWriter binaryWriter = new BinaryWriter(stream);

            stream.ReadTimeout = 3000;
            stream.WriteTimeout = 3000;

            bool isClientConnected = true;
            Interlocked.Increment(ref clientCount);
            UpdateConnectedClientCount();

            int failCounter = 0;

            while (isClientConnected)
            {
                try
                {
                    byte[] sizeBytes = binaryReader.ReadBytes(4);

                    if (sizeBytes.Length > 0)
                    {
                        int dataSize = BitConverter.ToInt32(sizeBytes, 0);

                        if (dataSize > 0)
                        {
                            byte[] bytes = binaryReader.ReadBytes(dataSize);

                            int msgType = BitConverter.ToInt32(bytes, 0);

                            if (bytes != null && Enum.IsDefined(typeof(MsgType), msgType))
                            {
                                switch ((MsgType)msgType)
                                {
                                    case MsgType.KeepAlive:
                                        TcpHandleKeepAlive(binaryWriter);
                                        break;

                                    case MsgType.Path:
                                        if (bytes.Length == sizeof(PathRequest))
                                        {
                                            TcpHandlePathRequest(binaryWriter, bytes);
                                        }
                                        else
                                        {
                                            LogQueue.Enqueue(new LogEntry("Received malformed PathRequest...", ConsoleColor.Red, $"{client.Client.RemoteEndPoint}", LogLevel.ERROR));
                                        }
                                        break;

                                    case MsgType.RandomPoint:
                                        if (bytes.Length == sizeof(RandomPointRequest))
                                        {
                                            Stopwatch sw = Stopwatch.StartNew();

                                            TcpHandleRandomPointRequest(binaryWriter, bytes);

                                            sw.Stop();
                                            LogQueue.Enqueue(new LogEntry("[RANDOMPOINT] ", ConsoleColor.Green, $"took {sw.ElapsedMilliseconds}ms ({sw.ElapsedTicks} ticks)", LogLevel.INFO));
                                        }
                                        else
                                        {
                                            LogQueue.Enqueue(new LogEntry("Received malformed RandomPointRequest...", ConsoleColor.Red, $"{client.Client.RemoteEndPoint}", LogLevel.ERROR));
                                        }
                                        break;
                                }
                            }
                        }
                    }
                    else
                    {
                        if (failCounter >= 5)
                        {
                            isClientConnected = false;
                        }

                        Thread.Sleep(500);
                        ++failCounter;
                    }
                }
                catch (IOException)
                {
                    // occurs when client disconnects
                    isClientConnected = false;
                }
                catch (Exception e)
                {
                    string errorMsg = $"{e.GetType()} occured at client: \n{e.StackTrace}";
                    LogQueue.Enqueue(new LogEntry(errorMsg, ConsoleColor.Red, $"{client.Client.RemoteEndPoint}", LogLevel.ERROR));

                    isClientConnected = false;
                }
            }

            Interlocked.Decrement(ref clientCount);
            UpdateConnectedClientCount();
            LogQueue.Enqueue(new LogEntry("Client disconnected: ", ConsoleColor.Cyan, client.Client.RemoteEndPoint.ToString()));
        }

        private static Settings LoadConfigFile()
        {
            Settings settings = null;

            try
            {
                if (File.Exists(SettingsPath))
                {
                    settings = JsonConvert.DeserializeObject<Settings>(File.ReadAllText(SettingsPath));

                    if (!settings.MmapsFolder.EndsWith("/") && !settings.MmapsFolder.EndsWith("\\"))
                    {
                        settings.MmapsFolder += "/";
                    }

                    if (settings.LogToFile)
                    {
                        LogCheckFileExistence();
                    }

                    LogQueue.Enqueue(new LogEntry($"MaxPolyPathCount = {settings.MaxPolyPathCount}", ConsoleColor.White));
                    LogQueue.Enqueue(new LogEntry($"MaxPointPathCount = {settings.MaxPointPathCount}", ConsoleColor.White));
                    LogQueue.Enqueue(new LogEntry("Loaded config file", ConsoleColor.Green));
                }
                else
                {
                    settings = new Settings();
                    File.WriteAllText(SettingsPath, JsonConvert.SerializeObject(settings));
                    LogQueue.Enqueue(new LogEntry("Created default config file", ConsoleColor.White));
                }
            }
            catch (Exception ex)
            {
                LogQueue.Enqueue(new LogEntry("Failed to parse config.json...\n", ConsoleColor.Red, ex.ToString(), LogLevel.ERROR));
            }

            return settings;
        }

        private static void LogCheckFileExistence()
        {
            // create the directory if needed
            if (!Directory.Exists(Settings.LogFilePath))
            {
                Directory.CreateDirectory(Settings.LogFilePath);

                // create the file if needed
                if (!File.Exists(Settings.LogFilePath))
                {
                    File.Create(Settings.LogFilePath).Close();
                }
                else
                {
                    // remove old logs if configured
                    if (Settings.RemoveOldLog)
                    {
                        File.Delete(Settings.LogFilePath);
                    }
                }
            }
        }

        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        private static void LogSetup()
        {
            LoggingThread = new Thread(() => LogThreadRoutine());
            LoggingThread.Start();
        }

        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        private static void PreloadMaps()
        {
            LogQueue.Enqueue(new LogEntry("Preloading Maps: ", ConsoleColor.Green, JsonConvert.SerializeObject(Settings.PreloadMaps), LogLevel.DEBUG));

            foreach (int i in Settings.PreloadMaps)
            {
                AmeisenNav.LoadMap(i);
                LogQueue.Enqueue(new LogEntry("Preloaded Map: ", ConsoleColor.Green, i.ToString(), LogLevel.DEBUG));
            }

            LogQueue.Enqueue(new LogEntry($"Preloaded {Settings.PreloadMaps.Length} Maps", ConsoleColor.Green, string.Empty, LogLevel.DEBUG));
        }

        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        private static void PrintHeader()
        {
            Console.ForegroundColor = ConsoleColor.White;
            string version = System.Reflection.Assembly.GetEntryAssembly().GetName().Version.ToString();
            Console.WriteLine(@"    ___                   _                 _   __           ");
            Console.WriteLine(@"   /   |  ____ ___  ___  (_)_______  ____  / | / /___ __   __");
            Console.WriteLine(@"  / /| | / __ `__ \/ _ \/ / ___/ _ \/ __ \/  |/ / __ `/ | / /");
            Console.WriteLine(@" / ___ |/ / / / / /  __/ (__  )  __/ / / / /|  / /_/ /| |/ / ");
            Console.WriteLine(@"/_/  |_/_/ /_/ /_/\___/_/____/\___/_/ /_/_/ |_/\__,_/ |___/  ");
            Console.Write("                                        Server ");
            Console.ForegroundColor = ConsoleColor.Yellow;
            Console.WriteLine(version);
            Console.WriteLine();
            Console.ResetColor();
        }

        private static void TcpHandleKeepAlive(BinaryWriter writer)
        {
            // write the respose to a hello world, (int size, byte 0x1)
            writer.Write(new byte[] { 1, 0, 0, 0, 1 }, 0, 5);
            writer.Flush();
        }

        private unsafe static void TcpHandlePathRequest(BinaryWriter writer, byte[] bytes)
        {
            PathRequest request = Utils.FromBytes<PathRequest>(bytes);

            lock (querylock)
            {
                if (!AmeisenNav.IsMapLoaded(request.MapId))
                {
                    LogQueue.Enqueue(new LogEntry("[MMAPS] ", ConsoleColor.Green, $"Loading Map: {request.MapId}", LogLevel.INFO));
                    AmeisenNav.LoadMap(request.MapId);
                }
            }

            Stopwatch sw = Stopwatch.StartNew();

            fixed (float* pStartPosition = request.A.ToArray())
            fixed (float* pEndPosition = request.B.ToArray())
            {
                switch (request.MovementType)
                {
                    case MovementType.FindPath:
                        int pathSize = 0;
                        float[] movePath = null;

                        try
                        {
                            lock (querylock)
                            {
                                movePath = AmeisenNav.GetPath(request.MapId, pStartPosition, pEndPosition, &pathSize);
                            }
                        }
                        catch { }

                        if (movePath != null && movePath.Length != 0)
                        {
                            // we can't apply the chaikin-curve on a path that has just a single node
                            if (request.Flags.HasFlag(PathRequestFlags.ChaikinCurve)
                                && pathSize > 1)
                            {
                                movePath = ChaikinCurve.Perform(movePath, Settings.ChaikinIterations);
                            }

                            // we can't apply the catmull-rom-spline on a path with less than 4 nodes
                            if (request.Flags.HasFlag(PathRequestFlags.CatmullRomSpline)
                                && pathSize >= 4)
                            {
                                movePath = CatmullRomSpline.Perform(movePath, Settings.CatmullRomSplinePoints);
                            }

                            pathSize = movePath.Length / 3;

                            fixed (float* pPath = movePath)
                            {
                                TcpSendData(writer, pPath, sizeof(Vector3) * pathSize);
                            }
                        }

                        sw.Stop();
                        LogQueue.Enqueue(new LogEntry("[FINDPATH] ", ConsoleColor.Green, $"took {sw.ElapsedMilliseconds}ms ({sw.ElapsedTicks} ticks) Nodes: {pathSize}/{Settings.MaxPointPathCount} Flags: {request.Flags}"));
                        break;

                    case MovementType.MoveAlongSurface:
                        float[] surfacePath;
                        lock (querylock) { surfacePath = AmeisenNav.MoveAlongSurface(request.MapId, pStartPosition, pEndPosition); }

                        fixed (float* pSurfacePath = surfacePath)
                        {
                            TcpSendData(writer, pSurfacePath, sizeof(Vector3));
                        }

                        sw.Stop();
                        // LogQueue.Enqueue(new LogEntry("[MOVEALONGSURFACE] ", ConsoleColor.Green, $"took {sw.ElapsedMilliseconds}ms ({sw.ElapsedTicks} ticks)"));
                        break;

                    case MovementType.CastMovementRay:
                        Vector3 castRayResult;
                        lock (querylock) { castRayResult = AmeisenNav.CastMovementRay(request.MapId, pStartPosition, pEndPosition) ? request.B : Vector3.Zero; }

                        TcpSendData(writer, &castRayResult, sizeof(Vector3));

                        sw.Stop();
                        LogQueue.Enqueue(new LogEntry("[CASTMOVEMENTRAY] ", ConsoleColor.Green, $"took {sw.ElapsedMilliseconds}ms ({sw.ElapsedTicks} ticks)"));
                        break;
                }
            }
        }

        private unsafe static void TcpHandleRandomPointRequest(BinaryWriter writer, byte[] bytes)
        {
            RandomPointRequest request = Utils.FromBytes<RandomPointRequest>(bytes);

            lock (querylock)
            {
                if (!AmeisenNav.IsMapLoaded(request.MapId))
                {
                    LogQueue.Enqueue(new LogEntry("[MMAPS] ", ConsoleColor.Green, $"Loading Map: {request.MapId}", LogLevel.INFO));
                    AmeisenNav.LoadMap(request.MapId);
                }
            }

            fixed (float* pointerStart = request.A.ToArray())
            {
                float[] randomPoint;

                lock (querylock)
                {
                    randomPoint = request.MaxRadius > 0f ? AmeisenNav.GetRandomPointAround(request.MapId, pointerStart, request.MaxRadius)
                                                         : AmeisenNav.GetRandomPoint(request.MapId);
                }

                fixed (float* pRandomPoint = randomPoint)
                {
                    TcpSendData(writer, pRandomPoint, sizeof(Vector3));
                }
            }
        }

        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        private unsafe static void TcpSendData<T>(BinaryWriter writer, T* data, int size) where T : unmanaged
        {
            // write the size of the data we're going to send
            writer.Write(BitConverter.GetBytes(size), 0, 4);

            // write the serialized data to the stream
            writer.Write(Utils.ToBytes(data, size), 0, size);
            writer.Flush();
        }

        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        private static void UpdateConnectedClientCount()
        {
            Console.Title = $"AmeisenNavigation Server - Connected Clients: [{clientCount}]";
        }
    }
}