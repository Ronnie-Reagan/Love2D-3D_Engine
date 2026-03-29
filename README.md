# TrueFlight

TrueFlight is a native C++ flight game/runtime built on SDL3, Vulkan, ImGui, and procedural world simulation.

This repository currently contains:

- A playable Windows native executable target: `TrueFlight`
- A native smoke-test target: `TrueFlightNativeSmoke`
- Procedural terrain/world generation and persistent world storage
- Flight, walking, terraforming, paint, HUD, instrument, and online/Steam menu systems
- Asset loading for built-in, `.glb`, `.gltf`, and `.stl` models

## Current Scope

The native port in this repo already implements these major systems:

- SDL3 windowing, input, audio, and GPU/Vulkan rendering
- Procedural terrain with chunk streaming, caves, tunnels, water, biome coloring, terraces, craters, and decoration controls
- Native flight dynamics, atmosphere/wind, propulsion, collision/crash handling, and live tuning
- Chase/cockpit flight camera plus on-foot walking mode and terrain editing tools
- Runtime model loading and per-model paint overlays saved as PNGs
- Configurable HUD and instrument panels
- World instance creation, selection, deletion, and persisted terrain/world metadata
- Native networking/replication foundations plus optional Steamworks integration

## Dependencies

Not everything is handled the same way:

- SDL3 is fetched automatically by CMake with `FetchContent`
- Dear ImGui is expected in `NativePort/third_party/imgui` and is already referenced by the build
- `glslangValidator` must be available from a Vulkan SDK install or on `PATH`
- Steamworks is optional, Windows-only, and only enabled when you provide a valid SDK

The build does not vendor the Vulkan SDK or Steamworks SDK.

## Build

From the workspace root:

```powershell
cmake -S . -B build/workspace-layout -G "Visual Studio 18 2026" -A x64
cmake --build build/workspace-layout --target TrueFlight --config Release
```

Or use the helper script:

```powershell
python NativePort\build_trueflight.py
```

Useful script options:

- `--ctest`: builds `TrueFlightNativeSmoke` and runs `ctest`
- `--enable-steamworks`: forces Steamworks on if a valid SDK is available
- `--disable-steamworks`: forces Steamworks off
- `--steamworks-sdk-root <path>`: points at a Steamworks SDK root or `sdk` directory
- `--no-package`: skips staging `build/package/<config>/TrueFlight`

Build outputs are staged under:

- `build/workspace-layout/NativePort/Release/TrueFlight.exe`
- `build/package/Release/TrueFlight/`

## Runtime Requirements

- Windows
- A Vulkan-capable GPU/driver stack that SDL3 can use through the Vulkan backend
- Shader compilation support via `glslangValidator` during build

If Steamworks is enabled, the build script/CMake flow also stages `steam_appid.txt` and `steam_api64.dll` beside the executable.

## Run

```powershell
.\build\workspace-layout\NativePort\Release\TrueFlight.exe
```

The executable also accepts a Steam lobby launch argument:

```powershell
.\build\workspace-layout\NativePort\Release\TrueFlight.exe +connect_lobby <lobby_id>
```

## Controls

Default flight controls:

- `W` / `S`: pitch forward/back
- `A` / `D`: roll left/right
- `Q` / `E`: yaw left/right
- `Up` / `Down`: throttle up/down
- Mouse move: flight stick input
- Mouse wheel: throttle
- `Ctrl` + mouse wheel: elevator trim
- `[` / `]`: rudder trim left/right
- Right mouse: zoom
- `Space`: air brakes
- `C`: chase/cockpit camera
- `M`: map toggle
- `F3`: debug toggle
- `R`: reset flight
- `Esc`: menu/pause

Walking and editing:

- `W` / `A` / `S` / `D`: move while on foot
- Mouse move: look
- `Shift`: sprint
- `Space`: jump
- `T`: toggle terraform mode
- `LMB`: fire projectiles
- `B`: drop bombs
- `A`: afterburner

Paint editing:

- `1`: brush mode
- `2`: erase mode
- `F`: fill
- `Ctrl+Z`: undo
- `Ctrl+Y`: redo
- `P`: commit paint

Voice:

- `V`: push-to-talk when the online voice mode uses PTT

All bindings are configurable in-game.

## In-Game Menus

The pause/menu flow currently includes:

- `Main`
- `Settings`
- `Characters`
- `Paint`
- `HUD`
- `Instruments`
- `Controls`
- `Help`

Settings sub-pages currently include:

- `Graphics`
- `Camera`
- `Sound`
- `Flight`
- `Terrain`
- `Lighting`
- `Online`

Character and paint tools are split across:

- `Plane`
- `Player`
- `Enemy`
- `Target`

## Persistence

The game uses SDL preference paths under:

- `SDL_GetPrefPath("Don Reagan", "TrueFlight")`

The runtime stores data there, including:

- `native_settings.ini`
- `HUD-settings.ini`
- `paint/`
- `worlds/`
- `terrain_chunk_cache/`

## Assets

Included sample assets currently include:

- `portSource/Assets/Models/DualEngine.glb`
- `portSource/Assets/Models/DualEngine2.glb`
- `portSource/Assets/Models/DualEngine.stl`

The runtime supports loading:

- `.glb`
- `.gltf`
- `.stl`

If no external model is selected, the game can fall back to built-in/native visual defaults.

## Testing

There is a native smoke-test executable and a registered CTest target:

```powershell
python NativePort\build_trueflight.py --ctest
```

At the time of this README update, the smoke-test target builds successfully, but the current `ctest` run is not fully passing.

## Repository Notes

- The top-level `CMakeLists.txt` is a workspace wrapper that builds `NativePort`
- Third-party license notices are collected in `licenses/THIRD_PARTY_LICENSES.txt`
- Main project license text is in `LICENSE.txt`
- Forks and sumbissions of changes are encouraged but must have permission given prior to posting of said submissions.
- This is Source-Availabe *Not* Open-Source.