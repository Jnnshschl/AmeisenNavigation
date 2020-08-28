using AmeisenNavigation.Server.Objects;
using AmeisenNavigation.Server.Objects.Enums;
using AmeisenNavigation.Server.Transformations;
using AmeisenNavigationWrapper;
using Newtonsoft.Json;
using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
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
        private static readonly string SettingsPath = AppDomain.CurrentDomain.BaseDirectory + "config.json";
        private static int clientCount = 0;
        private static bool stopServer = false;

        private static AmeisenNav AmeisenNav { get; set; }

        private static Thread LoggingThread { get; set; }

        private static ConcurrentQueue<LogEntry> LogQueue { get; set; }

        private static Settings Settings { get; set; }

        private static TcpListener TcpListener { get; set; }

        public static string BuildLog(string s, LogLevel logLevel)
            => $"[{DateTime.Now.ToLongTimeString()}] {$"[{logLevel}]",9} >> {s}";

        public static string ColoredPrint(string s, ConsoleColor color, string uncoloredOutput = "", LogLevel logLevel = LogLevel.INFO)
        {
            string logString = BuildLog(s, logLevel);

            Console.ForegroundColor = color;
            Console.Write(logString);
            Console.ResetColor();
            Console.WriteLine(uncoloredOutput);

            return $"{logString}{uncoloredOutput}";
        }

        public static void EnterServerLoop()
        {
            while (!stopServer)
            {
                try
                {
                    TcpClient newClient = TcpListener.AcceptTcpClient();
                    Thread userThread = new Thread(new ThreadStart(() => HandleClient(newClient)));
                    userThread.Start();
                }
                catch (Exception e)
                {
                    if (!stopServer)
                    {
                        string errorMsg = $"{e.GetType()} occured while at the TcpListener\n";
                        LogQueue.Enqueue(new LogEntry(errorMsg, ConsoleColor.Red, e.ToString(), LogLevel.ERROR));
                    }
                }
            }
        }

        public unsafe static void HandleClient(TcpClient client)
        {
            LogQueue.Enqueue(new LogEntry($"Client connected: {client.Client.RemoteEndPoint}", ConsoleColor.Cyan));

            using Stream stream = client.GetStream();

            // stream.ReadTimeout = 500;
            // stream.WriteTimeout = 500;

            using BinaryReader binaryReader = new BinaryReader(stream);
            using BinaryWriter binaryWriter = new BinaryWriter(stream);

            bool isClientConnected = true;
            Interlocked.Increment(ref clientCount);
            UpdateConnectedClientCount();

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
                                        HandleKeepAlive(binaryWriter);
                                        break;

                                    case MsgType.Path:

                                        if (bytes.Length == sizeof(PathRequest))
                                        {
                                            HandlePathRequest(binaryWriter, bytes);
                                        }
                                        else
                                        {
                                            LogQueue.Enqueue(new LogEntry("Received malformed PathRequest...", ConsoleColor.Red, $"{client.Client.RemoteEndPoint}", LogLevel.ERROR));
                                        }

                                        break;

                                    case MsgType.RandomPoint:

                                        if (bytes.Length == sizeof(RandomPointRequest))
                                        {
                                            Stopwatch swRandomPoint = new Stopwatch();
                                            swRandomPoint.Start();

                                            HandleRandomPointRequest(binaryWriter, bytes);

                                            swRandomPoint.Stop();
                                            LogQueue.Enqueue(new LogEntry("[RANDOMPOINT] ", ConsoleColor.Green, $"FindRandomPoint took {swRandomPoint.ElapsedMilliseconds}ms ({swRandomPoint.ElapsedTicks} ticks)", LogLevel.INFO));
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

        public static void LoggingThreadRoutine()
        {
            while (!stopServer || LogQueue.Count > 0)
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

            SetupLogging();

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

                EnterServerLoop();

                AmeisenNav.Dispose();
            }
        }

        private static void CheckForLogFileExistence()
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

        private static void HandleKeepAlive(BinaryWriter writer)
        {
            writer.Write(new byte[] { 1, 0, 0, 0, 1 }, 0, 5);
            writer.Flush();
        }

        private unsafe static void HandlePathRequest(BinaryWriter writer, byte[] bytes)
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

            fixed (float* pStartPosition = request.A.ToArray())
            fixed (float* pEndPosition = request.B.ToArray())
            {
                switch (request.MovementType)
                {
                    case MovementType.FindPath:
                        int pathSize = 0;
                        float[] movePath;

                        Stopwatch swPath = Stopwatch.StartNew();

                        try
                        {
                            lock (querylock) { movePath = AmeisenNav.GetPath(request.MapId, pStartPosition, pEndPosition, &pathSize); }
                        }
                        catch { movePath = null; }

                        List<Vector3> path = new List<Vector3>(movePath != null ? movePath.Length * 3 : 1);

                        if (movePath == null || movePath.Length == 0)
                        {
                            path.Add(Vector3.Zero);

                            fixed (Vector3* pPath = path.ToArray())
                                SendData(writer, pPath, sizeof(Vector3));

                            return;
                        }

                        for (int i = 0; i < pathSize * 3; i += 3)
                        {
                            path.Add(new Vector3(movePath[i], movePath[i + 1], movePath[i + 2]));
                        }

                        if (request.Flags.HasFlag(PathRequestFlags.ChaikinCurve))
                        {
                            if (path.Count > 1)
                            {
                                path = ChaikinCurve.Perform(path, Settings.ChaikinIterations);
                            }
                            else
                            {
                                request.Flags &= ~PathRequestFlags.ChaikinCurve;
                            }
                        }

                        if (request.Flags.HasFlag(PathRequestFlags.CatmullRomSpline))
                        {
                            if (path.Count >= 4)
                            {
                                path = CatmullRomSpline.Perform(path, Settings.CatmullRomSplinePoints);
                            }
                            else
                            {
                                request.Flags &= ~PathRequestFlags.CatmullRomSpline;
                            }
                        }

                        int size = sizeof(Vector3) * path.Count;

                        fixed (Vector3* pPath = path.ToArray())
                            SendData(writer, pPath, size);

                        swPath.Stop();
                        LogQueue.Enqueue(new LogEntry("[FINDPATH] ", ConsoleColor.Green, $"Generating path with {path.Count}/{Settings.MaxPointPathCount} nodes took {swPath.ElapsedMilliseconds}ms ({swPath.ElapsedTicks} ticks) (Flags: {request.Flags})", LogLevel.INFO));

                        break;

                    case MovementType.MoveAlongSurface:
                        Stopwatch swMoveAlongSurface = Stopwatch.StartNew();

                        float[] surfacePath;
                        lock (querylock) { surfacePath = AmeisenNav.MoveAlongSurface(request.MapId, pStartPosition, pEndPosition); }

                        fixed (float* pSurfacePath = surfacePath)
                            SendData(writer, pSurfacePath, sizeof(Vector3));

                        swMoveAlongSurface.Stop();
                        LogQueue.Enqueue(new LogEntry("[MOVEALONGSURFACE] ", ConsoleColor.Green, $"MoveAlongSurface took {swMoveAlongSurface.ElapsedMilliseconds}ms ({swMoveAlongSurface.ElapsedTicks} ticks)", LogLevel.INFO));

                        break;

                    case MovementType.CastMovementRay:
                        Stopwatch swCastMovementRay = Stopwatch.StartNew();

                        Vector3 castRayResult;
                        lock (querylock) { castRayResult = AmeisenNav.CastMovementRay(request.MapId, pStartPosition, pEndPosition) ? request.B : Vector3.Zero; }

                        SendData(writer, &castRayResult, sizeof(Vector3));

                        swCastMovementRay.Stop();
                        LogQueue.Enqueue(new LogEntry("[CASTMOVEMENTRAY] ", ConsoleColor.Green, $"CastMovementRay took {swCastMovementRay.ElapsedMilliseconds}ms ({swCastMovementRay.ElapsedTicks} ticks)", LogLevel.INFO));

                        break;
                }
            }
        }

        private unsafe static void HandleRandomPointRequest(BinaryWriter writer, byte[] bytes)
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
                    SendData(writer, pRandomPoint, sizeof(Vector3));
            }
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
                        CheckForLogFileExistence();
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

        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        private unsafe static void SendData<T>(BinaryWriter writer, T* data, int size) where T : unmanaged
        {
            writer.Write(BitConverter.GetBytes(size), 0, 4);
            writer.Write(Utils.ToBytes(data, size), 0, size);
            writer.Flush();
        }

        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        private static void SetupLogging()
        {
            LoggingThread = new Thread(() => LoggingThreadRoutine());
            LoggingThread.Start();
        }

        [MethodImpl(MethodImplOptions.AggressiveInlining)]
        private static void UpdateConnectedClientCount()
        {
            Console.Title = $"AmeisenNavigation Server - Connected Clients: [{clientCount}]";
        }
    }
}