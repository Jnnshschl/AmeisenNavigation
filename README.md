# AmeisenNavigation 🐜

TCP-based Navigation-Server for my WoW-Bot, utilizing *TrinityCore MMAPs* and *recastnavigation*. The AnTCP library supports a Windows-based server (Linux support planned for the future).

### What's Supported 🚀

* Straight Pathfinding
* Smooth Pathfinding (Chaikin Curve or Catmull-Rom Spline)
* Move small deltas with Navmesh
* Movement Raycasting
* Get a Random Point on Mesh

### What's Planned 🛠️

* (W.I.P) Polygon Exploration (send a polygon and get a planned path covering the area)
* Flying path generation
* Vmap integration for indoor checks and potentially more features
* Additional MMAP formats
* Linux support

Check out the Navigation-Server used in the AmeisenBotX on this [YouTube channel](https://www.youtube.com/channel/UCxwiiRjjQVETtatGzKAoIcQ).

## How to 📝

1. Download the latest release [here](https://github.com/Jnnshschl/AmeisenNavigation/releases).
2. Run the server to create the `config.json` and customize it as needed.
3. Set the correct MMAPs version:
   - 0: TrinityCore 3.3.5a
   - 1: SkyFire 5.4.8
4. Specify the MMAPs folder location:
   - Export the MMAPs using TrinityCore tools (recommended)
   - Or download MMAPs from the internet (may cause errors due to old versions)
5. Start the server

## Credits 🙌

* TrinityCore - [GitHub](https://github.com/TrinityCore/TrinityCore)
* recastnavigation - [GitHub](https://github.com/recastnavigation/recastnavigation)
