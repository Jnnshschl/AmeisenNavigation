# AmeisenNavigation
Navigation Tcp-Server for my WoW-Bot based on the *TrinityCore MMAP's* and *Recast & Detour*

## How to
1. Clone this repository
```bash
git clone https://github.com/Jnnshschl/AmeisenNavigation.git
```
2. Make sure you got the MMAP's somewhere (you just need the mmaps, no map or vmap shizzle)
3. Open the .sln file in Visual Studio
4. Compile & start it...

Now there should be a TCP server running on the port you specified (default: 47110).

To generate a path you need to send a serialized *PathRequest* to this server:
```c#
struct PathRequest
{
    public Vector3 A { get; set; } // Starting position
    public Vector3 B { get; set; } // Target position
    public int MapId { get; set; } // your current MapId (Eastern Kingdoms = 0, Kalimdor = 1, ...)

    public PathRequest(Vector3 a, Vector3 b, int mapId)
    {
        A = a;
        B = b;
        MapId = mapId;
    }
}

public struct Vector3
{
    public float X { get; set; }
    public float Y { get; set; }
    public float Z { get; set; }

    public Vector3(float x, float y, float z)
    {
        X = x;
        Y = y;
        Z = z;
    }
}
```
Serialize this struct with a serializer, for example Newtonsoft Json:
```c#
JsonConvert.SerializeObject(stringToSend);
```
Send it using the C# TcpClient Class as an ASCII string.

After you sent the request you will get the path as a serialized *List* of *Vector3's*.

Deserialize this Json with a deserializer, for example Newtonsoft Json:
```c#
JsonConvert.DeserializeObject<List<Vector3>>(receivedString);
```

## How to get these MMAP's
Either generate them yourself using the generator from TrinityCore

https://github.com/TrinityCore/TrinityCore

or use the ones supplied in this repack, go check it out it's great

[AC-Web Ultimate Repack 3.3.5a](http://www.ac-web.org/forums/showthread.php?211443-Official-AC-Web-Ultimate-Repack-(3-3-5a)(Eluna-Engine))

## Additional Information

⚠️**If its the first request on the MapId, the server first has to load the mmaps for that continent, which can take some time based on your hardware!**

If you want to, you can preload specific mmaps on startup, to do that add the following line into the *AmeisenNavigation.Wrapper.h*
```c++
AmeisenNav(String^ mmap_dir) 
{ 
    ameisen_nav = new AmeisenNavigation(std::string(string_to_char_array(mmap_dir)));
    ameisen_nav->LoadMmapsForContinent(0); // this will preload Eastern Kingdoms
}
```

## Credits

❤️ TrinityCore for their MMAP format - https://github.com/TrinityCore/TrinityCore

❤️ Recast & Detour - https://github.com/recastnavigation/recastnavigation
