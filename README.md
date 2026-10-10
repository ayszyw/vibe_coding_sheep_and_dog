# Sheepdog Top-Down Portrait Demo

A small C++20 + raylib demo about herding scattered sheep through the gate and into the pen.

## Requirements

- CMake 3.22+
- A C++20 compiler
- Ninja or another CMake-supported generator
- Git and network access for the first build

The project fetches raylib 6.0 from GitHub automatically with CMake FetchContent.

## Build

```powershell
./build.ps1
```

Or configure and build manually:

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

The executable is generated at `build/sheepdog-demo.exe`.

## Play

Run `build/sheepdog-demo.exe`.

- `WASD` or arrow keys: move the dog
- `Shift`: sprint
- `Space`: bark to push nearby sheep
- `P`: pause or resume
- `R`: restart
- `Enter`: restart after victory

Drive all 60 sheep through the glowing gate. Each sheep is worth 100 points. Finishing before three minutes adds a time bonus.

## Replaceable Art

All character and fence art is loaded from the `assets/` directory.

- `dog.png`: 96x96 transparent PNG. The sprite should face up. Collision bounds are calculated from the visible alpha pixels.
- `sheep.png`: 64x64 transparent PNG. The sprite should face up.
- `fence.png`: 64x24 transparent PNG. The rail runs horizontally with a post at the left edge.

Replace the file with another PNG using the same name. If an image is missing, the game falls back to its original procedural drawing.

## Smoke Test

Run `build/sheepdog-demo.exe --smoke-test`. It initializes the renderer and audio device, renders for about 1.5 seconds, exports `smoke-screenshot.png`, and exits.
