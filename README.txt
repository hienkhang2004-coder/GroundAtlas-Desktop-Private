GroundAtlas Desktop — Native 1.3.0 (100% Pure C++20 / Qt 6.8 / OpenGL 3.3 / MapLibre Native)
Updated 25 November 2026, Asia/Bangkok

OVERVIEW
GroundAtlas Desktop 1.3.0 is a 100% compiled native desktop workstation built in
C++20, Qt 6.8.3, OpenGL 3.3, and MapLibre Native for Qt 3.0.0.
Zero Chromium, zero Electron, zero WebView, zero HTML/JS runtime.

NEW IN 1.3.0
- Global OpenStreetMap Site Loading Fix:
  * Prioritizes fast global Overpass mirror (overpass.openstreetmap.fr) with
    automatic multi-mirror failover (overpass-api.de, lz4.overpass-api.de,
    overpass.private.coffee).
  * Unlocks map boundary selection by default and automatically switches data
    source to OpenStreetMap when picking any site outside the active project.
- Default Architectural Study Presentation:
  * Freshly loaded sites and bundled startup sample default to Suushka · Study
    with the Amenities (Tien ich) layer unchecked cleanly.
- Hardware Auto-Profiler & High-Refresh-Rate Engine (60–500 Hz):
  * Auto-detects GPU, CPU threads, RAM, and display refresh rate (180/240/360/500 Hz)
    with selectable Auto / Low (1024px 4-tap PCF, 60 Hz) / Balanced (2048px 9-tap,
    up to 144 Hz) / Ultra (4096px 16-tap Rotated Poisson Disk PCF, up to 500 Hz).
  * Optional smooth inertia damping for wheel zoom & 3D orbit synced to display Hz.
- Zero-Rebuild Continuous Real-Time 3D Sun & Shadows:
  * Splits static city VBO from dynamic Sun-Path Dome VBO (<0.1 ms rebuild) so
    dragging the continuous 1-minute Sun Hour slider or playing the delta-time Sun
    Cycle (0.5x / 1x / 2x) updates crisp 4096px GPU shadows smoothly up to 500 FPS.
- Studio Power Upgrades:
  * Direct vertex-handle drag & whole-plot move on Design Plots (2D/3D)
  * Quick Distance & Area Ruler Tool (M key & floating dock button)
  * GPU Architectural Crease Accent, Auto-Dark Basemap Softening & 3D DEM Terrain
  * Clean 4K (3840x2160) & 8K (7680x4320) Transparent Alpha PNG rendering
  * Undo / Redo (Ctrl+Z / Ctrl+Y), Spotlight Command Palette (Ctrl+P), and
    persistent workspace preferences (QSettings).

RUN
Open GroundAtlas.exe from the complete portable-1.3.0 folder. Keep its Qt/MinGW
DLLs, plugins, QMapLibre.dll, and data/ folder alongside GroundAtlas.exe.

THIRD-PARTY COMPONENTS & LICENSES
Qt 6.8.3 (LGPL/GPL), MinGW-w64 runtime, MapLibre Native for Qt 3.0.0 (BSD-2-Clause),
and earcut.hpp 2.2.4 (ISC). Full notices are retained under licenses/.
