using System;
using System.Linq;
using AmeisenNavigation.Client;

// Runs the C# client against a server serving the synthetic test world (tests/TestWorld.hpp, map 1).
// Usage: ClientIntegration <port>

if (args.Length < 1 || !int.TryParse(args[0], out int port))
{
    Console.WriteLine("Usage: ClientIntegration <port>");
    return 2;
}

const int map = 1;
int failures = 0;

// RD -> WoW: (x, y, z)_wow = (rdZ, rdX, rdY), same helper as the C++ tests.
static Vector3 Wow(float x, float y, float z) => new(z, x, y);

void Check(bool ok, string what)
{
    Console.WriteLine($"{(ok ? "OK  " : "FAIL")} {what}");
    if (!ok) failures++;
}

using var nav = new AmeisenNavClient("127.0.0.1", port) { TimeoutMs = 10000 };

if (!nav.TryConnect())
{
    Console.WriteLine("connect failed");
    return 1;
}

var cfg = nav.GetConfig();
Check(cfg is { UseAnpFileFormat: true, ProtocolVersion: >= 2, MaxPointPath: > 0 } && cfg.ServerVersion.Length > 0,
      $"GetConfig {cfg}");

nav.SetClientState(ClientState.NormalHorde);
nav.SetAreaCosts(1f, 0.75f, 1.6f, 4f, allyMult: 3f, hordeMult: 1f);
Check(nav.ApplyFilter() && !nav.IsFilterDirty, "ApplyFilter (ANP costs)");

var start = Wow(-700, 0, -300);
var end = Wow(-200, 0, -300);
var path = nav.GetPath(map, start, end);
Check(path is { Length: >= 3 } && path[^1].DistanceTo(end) < 1.5f, $"GetPath points={path?.Length}");

foreach (var flags in new[] { PathFlags.SmoothChaikin, PathFlags.SmoothCatmullRom | PathFlags.ValidateMoveAlongSurface,
                              PathFlags.SmoothBezier | PathFlags.ValidateClosestPointOnPoly, PathFlags.RequireComplete })
{
    var p = nav.GetPath(map, start, end, flags);
    Check(p is { Length: >= 2 }, $"GetPath {flags} points={p?.Length}");
}

Check(nav.GetRandomPath(map, start, end) is { Length: >= 2 }, "GetRandomPath");

// The platform deck is an island: partial path by default, null with RequireComplete.
var ground = Wow(-600, 0, -425);
var deck = Wow(-425, 5, -425);
Check(nav.GetPath(map, ground, deck) is { Length: >= 2 }, "GetPath to unreachable deck (partial)");
Check(nav.GetPath(map, ground, deck, PathFlags.RequireComplete) == null, "GetPath RequireComplete -> null");

bool clear = nav.CastRay(map, Wow(-400, 0, -300), Wow(-200, 0, -300), out var hitPoint);
Check(!clear && hitPoint.IsZero, "CastRay blocked (legacy)");
var hit = nav.CastRayEx(map, Wow(-400, 0, -300), Wow(-200, 0, -300));
Check(hit.IsValid && hit.IsHit && Math.Abs(hit.Position.Y - (-300)) < 4, $"CastRayEx {hit}");
var open = nav.CastRayEx(map, Wow(-400, 0, -50), Wow(-200, 0, -50));
Check(open.IsClear && open.Fraction == 1f, $"CastRayEx clear {open}");

var h = nav.GetHeight(map, Wow(-425, 6, -425));
Check(Math.Abs(h.Z - 5) < 0.6, $"GetHeight deck {h}");
Check(!nav.GetRandomPoint(map).IsZero, "GetRandomPoint");
Check(nav.GetRandomPointAround(map, start, 15f).DistanceTo(start) < 60f, "GetRandomPointAround");
Check(nav.MoveAlongSurface(map, Wow(-320, 0, -300), Wow(-280, 0, -300)).Y < -300, "MoveAlongSurface blocked");

var outline = new[] { Wow(-390, 0, -450), Wow(-210, 0, -450), Wow(-210, 0, -150), Wow(-390, 0, -150) };
var route = nav.ExplorePolygon(map, Wow(-380, 0, -440), outline, 40f);
Check(route is { Length: > 20 } && route.Length <= cfg!.MaxPointPath && route.All(p => Math.Abs(p.Z) < 0.6f),
      $"ExplorePolygon points={route?.Length}");
Check(nav.ExplorePolygon(map, start, outline.Take(2).ToArray(), 40f) == null, "ExplorePolygon invalid -> null");

Check(nav.GetPath(4242, Wow(0, 0, 0), Wow(1, 0, 1)) == null, "unknown map -> null");

Console.WriteLine(failures == 0 ? "ALL OK" : $"{failures} FAILED");
return failures == 0 ? 0 : 1;
