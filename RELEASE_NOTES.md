# Gears of War: Judgment for PC, v0.1.0

First public release. A native Windows build of the Xbox 360 game, made by statically recompiling its
code. **No game data is included; you need your own copy of the retail game** (title ID `4D530A26`).

## Download

`GearsOfWarJudgment-PC-v0.1.0.zip` contains two programs and nothing else to install:

- `GearsOfWarJudgment.exe`: the game, one self-contained file
- `Graphics Settings.exe`: quality, resolution, filtering, anti-aliasing, FSR, frame limit and mouse settings

Unzip to a writable folder, double-click `GearsOfWarJudgment.exe`, and pick your game folder the first time.
Checksums are in `SHA256SUMS.txt`.

## What is in this release

- Keyboard and mouse play with raw mouse look, mouse-driven menus, controller support
- Frame limiter (default 60), full unlock, F9 fps counter, F8 limiter toggle
- F4 in-game settings menu and a separate settings window with Low / Medium / High / Auto quality levels
- SMAA and FSR 1.0 upscaling and sharpening, 16x texture filtering, 1x or 2x internal resolution (2x is 2560x1440)
- Fixes: save games, motion blur removal, film grain and FXAA options, shader pipeline cache (no compile stutter after the first run)
- Full controls table in the README

## Known issues

- Only the early part of the campaign has been tested. Multiplayer and online features do not work.
- Tested only on an NVIDIA RTX 5070 Ti with Windows 11. AMD, Intel and handheld GPUs are untested.
- CPU limited: about 90 to 110 fps unlocked on a Ryzen 7 5800X3D; a slower CPU gives less.
- For an unlocked frame rate, set Frame limit to Unlocked in Graphics Settings.exe. F8 in game does not turn off vsync, so it may stay at half your monitor's refresh rate.
- Unlocked can stutter or tear without a G-Sync or FreeSync monitor. Use the 60 fps limit if so.
- Small hitches in busy scenes, about one frame in 200.
- Internal resolution above 2x is not supported.
- Shaders and pipelines are built on the first run, so the first minutes can be less smooth.

Build it yourself: see `BUILDING.md`.
