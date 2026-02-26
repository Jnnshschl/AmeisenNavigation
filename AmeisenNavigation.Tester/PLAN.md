# AmeisenNavigation Tester - UI Overhaul & Minimap Tiles Plan

## Overview
Complete overhaul of the WPF tester app: dark theme, proper layout, zoomable minimap tile canvas with path overlay. No new NuGet dependencies — P/Invoke StormLib directly, write a minimal BLP2 decoder in C#.

## New File Structure
```
AmeisenNavigation.Tester/
  AmeisenNavigation.Tester.csproj   (MODIFY - no new packages)
  App.xaml                          (MODIFY - load dark theme)
  MainWindow.xaml                   (REWRITE - proper layout)
  MainWindow.xaml.cs                (REWRITE - thin coordinator)
  MapDefinitions.cs                 (RENAME ContientSource.cs + add internal map names)
  Vector3.cs                        (NO CHANGE)

  Themes/
    DarkTheme.xaml                  (NEW - dark resource dictionary)

  Services/
    NavmeshClient.cs                (NEW - extracted server communication)
    StormLib.cs                     (NEW - P/Invoke wrapper for StormLib.dll)
    MpqArchiveSet.cs                (NEW - open all MPQs, extract files)
    BlpReader.cs                    (NEW - minimal BLP2 → BitmapSource decoder)
    TileCache.cs                    (NEW - memory + disk PNG cache)

  Controls/
    MapCanvas.cs                    (NEW - zoomable/pannable tile canvas with path overlay)

  Converters/
    WowCoordinateConverter.cs       (NEW - world coords ↔ tile/pixel coords)
```

## Phase 1: Foundation (no visual changes)

### 1.1 — MapDefinitions.cs (rename ContientSource.cs)
- Add `InternalNames` dictionary: `0→"Azeroth"`, `1→"Kalimdor"`, `530→"Expansion01"`, `571→"Northrend"`
- Fix typo `GetChoises` → `GetChoices`

### 1.2 — WowCoordinateConverter.cs
- Constants: `TileSize=533.33333f`, `WorldSize=TileSize*32`, `TileCount=64`, `TilePixelSize=256`
- `WorldToTile(worldX, worldY) → (tileX, tileY)` using `(int)(32 - world/TileSize)`
- `WorldToPixel(worldX, worldY) → (px, py)` for canvas positioning
- `PixelToWorld(px, py) → (worldX, worldY)` for click-to-coordinate

### 1.3 — NavmeshClient.cs
- Extract `AnTcpClient` wrapper, `MessageType` enum, `PathType` enum, `FilterConfig` struct from MainWindow.xaml.cs
- Methods: `TryConnect()`, `GetPath()`, `GetRandomPoint()`, `ConfigureFilter()`

### 1.4 — StormLib.cs (P/Invoke)
- Direct DllImport of the existing `StormLib.dll` from `dep/StormLib/{Win32|x64}/`
- Copy correct DLL to output dir via csproj
- Functions needed:
  - `SFileOpenArchive(string path, uint priority, uint flags, out IntPtr handle) → bool`
  - `SFileCloseArchive(IntPtr handle) → bool`
  - `SFileHasFile(IntPtr mpq, string fileName) → bool`
  - `SFileOpenFileEx(IntPtr mpq, string fileName, uint searchScope, out IntPtr fileHandle) → bool`
  - `SFileGetFileSize(IntPtr file, IntPtr high) → uint`
  - `SFileReadFile(IntPtr file, byte[] buffer, uint toRead, out uint read, IntPtr overlapped) → bool`
  - `SFileCloseFile(IntPtr file) → bool`

### 1.5 — MpqArchiveSet.cs
- Constructor takes WoW Data directory path
- Recursively finds all `.mpq` files, natural-sorts descending (matching C++ MpqManager)
- Opens all with `SFileOpenArchive` (read-only)
- `byte[]? ReadFile(string mpqPath)` — tries each archive, returns raw bytes
- `Dispose()` — closes all handles

## Phase 2: BLP Decoding

### 2.1 — BlpReader.cs
- Minimal BLP2 decoder (no dependencies)
- BLP2 header: magic "BLP2", type, encoding, alphaDepth, alphaEncoding, mipCount, width, height, mipOffsets[16], mipSizes[16]
- Support encoding types found in minimap tiles:
  - **DXT1** (most common for minimap) — implement S3TC DXT1 4x4 block decompression
  - **DXT3** — DXT with explicit alpha
  - **Uncompressed palette** — 256-color palette + indices
- `static BitmapSource Decode(byte[] blpData)` → returns frozen WPF BitmapSource (BGRA32)
- Keep it simple: only mip level 0 (full res, typically 256×256)

## Phase 3: Tile Cache

### 3.1 — TileCache.cs
- Memory cache: `Dictionary<(string map, int x, int y), BitmapSource>`
- Disk cache: `%LOCALAPPDATA%/AmeisenNavigation/TileCache/{map}/map{y:D2}_{x:D2}.png`
- `GetTile(mapName, tileX, tileY)` → check memory → check disk PNG → decode from MPQ → save PNG → cache
- `ClearMemoryCache()` on map switch
- All BitmapSources are `Freeze()`d for thread safety

## Phase 4: Dark Theme

### 4.1 — Themes/DarkTheme.xaml
- Colors: `#1e1e1e` (bg dark), `#2d2d2d` (bg medium), `#3e3e3e` (bg light), `#555` (border), `#e0e0e0` (text), `#0078d4` (accent)
- Implicit styles for Window, Button, TextBox, ComboBox, Label, GroupBox, RadioButton, CheckBox, Slider, ListBox, StatusBar, ScrollViewer
- Flat button style with rounded corners, accent hover
- GroupBox with dark header

### 4.2 — App.xaml
- Load `Themes/DarkTheme.xaml` via MergedDictionaries

## Phase 5: Map Canvas

### 5.1 — Controls/MapCanvas.cs
- Custom `FrameworkElement` with `OnRender(DrawingContext)` override
- **Tile rendering**: Calculate visible tile range from zoom/offset, draw each tile via `dc.DrawImage()`
- **Zoom**: Mouse wheel → scale around cursor, clamp 0.05–10.0
- **Pan**: Left-click drag → translate offset
- **Path overlay**: Draw path lines (yellow), start marker (green), end marker (red), intermediate nodes (white)
- **Click interaction**: Right-click sets end point, Shift+right-click sets start point
- **Events**: `StartPointSet`, `EndPointSet`, `CursorMoved` (for status bar coordinates)
- Only loads tiles that are currently visible (typically 10-30 at a time)

## Phase 6: UI Assembly

### 6.1 — MainWindow.xaml (REWRITE)
- Window 1200×700, min 1000×600
- Two-column Grid with GridSplitter:
  - **Left**: MapCanvas (fills available space)
  - **Right** (300px): ScrollViewer with StackPanel of GroupBoxes:
    - Data Source (folder picker for WoW Data dir)
    - Connection (map dropdown, Get Path button, status indicator)
    - Coordinates (Start/End X,Y,Z textboxes + Random buttons, laid out in a proper Grid)
    - Path Options (path type radios, smoothing checkboxes, validation checkboxes)
    - Filter Config (client state radios, area cost sliders with labels)
    - Stats (point count, distance, time)
    - Path Points (listbox, collapsible)
- StatusBar at bottom: connection status, world coordinates under cursor, tile index

### 6.2 — MainWindow.xaml.cs (REWRITE)
- Thin coordinator wiring services together
- Initializes NavmeshClient, MpqArchiveSet, TileCache, MapCanvas
- Browse button → FolderBrowserDialog → init MPQ pipeline → pass TileCache to MapCanvas
- Map change → clear cache, update MapCanvas map name
- Get Path → query server → set MapCanvas path → fit view → update stats
- MapCanvas events → update coordinate textboxes and status bar
- Save/restore data directory path in a simple settings file

## Phase 7: Build Config

### 7.1 — csproj changes
- Add `Content` items to copy `StormLib.dll` (pick Win32 or x64 based on PlatformTarget) to output
- Since project is x86, copy `dep/StormLib/Win32/StormLib.dll`
- No new NuGet packages

## Key Design Decisions
- **No MVVM** — keep code-behind with extracted services, this is a dev tool
- **FrameworkElement.OnRender** for map canvas — GPU-accelerated DrawImage, no UIElement tree overhead
- **P/Invoke StormLib directly** — no NuGet wrapper, DLLs already in dep/
- **Hand-rolled BLP2 decoder** — DXT1/DXT3 decompression is ~150 lines of code, avoids dependency
- **PNG disk cache** — decode BLP once, subsequent loads are fast PNG reads
