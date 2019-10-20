using AmeisenNavigation.Server.Objects;
using AmeisenNavigation.Server.Transformations;
using AmeisenNavigationWrapper;
using Newtonsoft.Json;
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Net;
using System.Net.Sockets;
using System.Text;
using System.Threading;

namespace AmeisenNavigation.Server
{
    internal static class Program
    {
        private static readonly string SettingsPath = AppDomain.CurrentDomain.BaseDirectory + "config.json";
        private static int clientCount = 0;
        private static readonly object maplock = new object();

        private static bool stopServer = false;

        private static TcpListener TcpListener { get; set; }

        private static AmeisenNav AmeisenNav { get; set; }

        private static Thread LoggingThread { get; set; }

        private static Queue<LogEntry> LogQueue { get; set; }

        private static Settings Settings { get; set; }

        public static void Main()
        {
            // buggy need to find a beter way
            //// Console.CancelKeyPress += Console_CancelKeyPress;

            PrintHeader();

            LogQueue = new Queue<LogEntry>();
            Settings = LoadConfigFile();

            SetupLogging();

            UpdateConnectedClientCount();

            if (Settings == null)
            {
                Console.ReadKey();
            }
            else if (!Directory.Exists(Settings.MmapsFolder))
            {
                LogQueue.Enqueue(new LogEntry($"MMAP folder missing, edit folder in config.json...", ConsoleColor.Red, string.Empty, LogLevel.ERROR));
                Console.ReadKey();
            }
            else
            {
                AmeisenNav = new AmeisenNav(Settings.MmapsFolder.Replace('/', '\\'));

                if (Settings.PreloadMaps.Length > 0)
                {
                    PreloadMaps();
                }

                TcpListener = new TcpListener(IPAddress.Parse(Settings.IpAddress), Settings.Port);
                TcpListener.Start();

                LogQueue.Enqueue(new LogEntry($"{Settings.IpAddress}:{Settings.Port} press Ctrl + C to exit...", ConsoleColor.Green, string.Empty, LogLevel.MASTER));

                EnterServerLoop();

                // cleanup after server stopped
                AmeisenNav.Dispose();
            }
        }

        private static void Console_CancelKeyPress(object sender, ConsoleCancelEventArgs e)
        {
            LogQueue.Enqueue(new LogEntry($"Stopping server...", ConsoleColor.Red, string.Empty, LogLevel.MASTER));
            stopServer = true;
            TcpListener.Stop();
            e.Cancel = true;
        }

        private static void SetupLogging()
        {
            LoggingThread = new Thread(() => LoggingThreadRoutine());
            LoggingThread.Start();
        }

        public static List<Vector3> GetPath(Vector3 start, Vector3 end, int mapId, PathRequestFlags flags, string clientIp)
        {
            int pathSize;
            List<Vector3> path = new List<Vector3>();

            Stopwatch sw = new Stopwatch();
            sw.Start();

            lock (maplock)
            {
                if (!AmeisenNav.IsMapLoaded(mapId))
                {
                    AmeisenNav.LoadMap(mapId);
                }

                unsafe
                {
                    fixed (float* pointerStart = start.ToArray())
                    fixed (float* pointerEnd = end.ToArray())
                    {
                        float* path_raw = AmeisenNav.GetPath(mapId, pointerStart, pointerEnd, &pathSize);

                        // postprocess the raw path to a list of Vector3
                        // the raw path looks like this:
                        // [ x1, y1, z1, x2, y2, z2, ...]
                        for (int i = 0; i < pathSize * 3; i += 3)
                        {
                            path.Add(new Vector3(path_raw[i], path_raw[i + 1], path_raw[i + 2]));
                        }
                    }
                }
            }

            if (flags.HasFlag(PathRequestFlags.ChaikinCurve))
            {
                path = ChaikinCurve.Perform(path);
            }

            sw.Stop();
            LogQueue.Enqueue(new LogEntry($"[{clientIp}] ", ConsoleColor.Green, $"Building Path with {path.Count} Nodes took {sw.ElapsedMilliseconds}ms", LogLevel.INFO));

            return path;
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

        public static void HandleClient(TcpClient client)
        {
            LogQueue.Enqueue(new LogEntry($"Client connected: {client.Client.RemoteEndPoint}", ConsoleColor.Cyan));

            using (StreamReader reader = new StreamReader(client.GetStream(), Encoding.ASCII))
            using (StreamWriter writer = new StreamWriter(client.GetStream(), Encoding.ASCII))
            {
                bool isClientConnected = true;
                Interlocked.Increment(ref clientCount);
                UpdateConnectedClientCount();

                while (isClientConnected)
                {
                    try
                    {
                        string rawData = reader.ReadLine().Replace("&gt;", string.Empty);
                        if (!string.IsNullOrEmpty(rawData))
                        {
                            PathRequest pathRequest = JsonConvert.DeserializeObject<PathRequest>(rawData);

                            List<Vector3> path = GetPath(pathRequest.A, pathRequest.B, pathRequest.MapId, pathRequest.Flags, client.Client.RemoteEndPoint.ToString());

                            writer.WriteLine(JsonConvert.SerializeObject(path) + " &gt;");
                            writer.Flush();
                        }
                    }
                    catch (Exception e)
                    {
                        string errorMsg = $"{e.GetType()} occured at client ";
                        LogQueue.Enqueue(new LogEntry(errorMsg, ConsoleColor.Red, $"{client.Client.RemoteEndPoint}", LogLevel.ERROR));

                        isClientConnected = false;
                    }
                }

                Interlocked.Decrement(ref clientCount);
                UpdateConnectedClientCount();
                LogQueue.Enqueue(new LogEntry($"Client disconnected: ", ConsoleColor.Cyan, client.Client.RemoteEndPoint.ToString()));
            }
        }

        public static string ColoredPrint(string s, ConsoleColor color, string uncoloredOutput = "", LogLevel logLevel = LogLevel.INFO)
        {
            string logString = BuildLog(s, logLevel);

            Console.ForegroundColor = color;
            Console.Write(logString);
            Console.ResetColor();
            Console.WriteLine(uncoloredOutput);

            return $"{logString}{uncoloredOutput}";
        }

        public static string BuildLog(string s, LogLevel logLevel)
            => $"[{DateTime.Now.ToLongTimeString()}] {$"[{logLevel.ToString()}]".PadRight(9)} >> {s}";

        public static void LoggingThreadRoutine()
        {
            while (!stopServer || LogQueue.Count > 0)
            {
                if (LogQueue.Count > 0)
                {
                    LogEntry logEntry = LogQueue.Dequeue();
                    if (logEntry.LogLevel >= Settings.LogLevel)
                    {
                        string logString = ColoredPrint(logEntry.ColoredPart, logEntry.Color, logEntry.UncoloredPart, logEntry.LogLevel);
                        if (Settings.LogToFile)
                        {
                            try
                            {
                                File.AppendAllText(Settings.LogFilePath, logString);
                            }
                            catch
                            {
                                // ignored, if we cant write to file we cant log it lmao
                            }
                        }
                    }
                }

                Thread.Sleep(1);
            }
        }

        private static void PrintHeader()
        {
            Console.ForegroundColor = ConsoleColor.White;
            string version = System.Reflection.Assembly.GetEntryAssembly().GetName().Version.ToString();
            Console.WriteLine(@"    ___                   _                 _   __           ");
            Console.WriteLine(@"   /   |  ____ ___  ___  (_)_______  ____  / | / /___ __   __");
            Console.WriteLine(@"  / /| | / __ `__ \/ _ \/ / ___/ _ \/ __ \/  |/ / __ `/ | / /");
            Console.WriteLine(@" / ___ |/ / / / / /  __/ (__  )  __/ / / / /|  / /_/ /| |/ / ");
            Console.WriteLine(@"/_/  |_/_/ /_/ /_/\___/_/____/\___/_/ /_/_/ |_/\__,_/ |___/  ");
            Console.Write($"                                        Server ");
            Console.ForegroundColor = ConsoleColor.Yellow;
            Console.WriteLine(version);
            Console.WriteLine();
            Console.ResetColor();
        }

        private static void PreloadMaps()
        {
            LogQueue.Enqueue(new LogEntry($"Preloading Maps: ", ConsoleColor.Green, JsonConvert.SerializeObject(Settings.PreloadMaps), LogLevel.DEBUG));
            foreach (int i in Settings.PreloadMaps)
            {
                AmeisenNav.LoadMap(i);
                LogQueue.Enqueue(new LogEntry($"Preloaded Map: ", ConsoleColor.Green, i.ToString(), LogLevel.DEBUG));
            }

            LogQueue.Enqueue(new LogEntry($"Preloaded {Settings.PreloadMaps.Length} Maps", ConsoleColor.Green, string.Empty, LogLevel.DEBUG));
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

                    LogQueue.Enqueue(new LogEntry($"Loaded config file", ConsoleColor.Green));
                }
                else
                {
                    settings = new Settings();
                    File.WriteAllText(SettingsPath, JsonConvert.SerializeObject(settings));
                    LogQueue.Enqueue(new LogEntry($"Created default config file", ConsoleColor.White));
                }
            }
            catch (Exception ex)
            {
                LogQueue.Enqueue(new LogEntry($"Failed to parse config.json...\n", ConsoleColor.Red, ex.ToString(), LogLevel.ERROR));
            }

            return settings;
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

        private static void UpdateConnectedClientCount()
        {
            Console.Title = $"AmeisenNavigation Server - Connected Clients: [{clientCount}]";
        }
    }
}
