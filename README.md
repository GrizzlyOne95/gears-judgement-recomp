# Gears of War: Judgment for PC

A native Windows build of **Gears of War: Judgment** (Xbox 360, People Can Fly, 2013), made by
statically recompiling the original game code to x64 and running it on an emulated Xbox 360
graphics and system layer (the [ReXGlue SDK](https://github.com/rexglue/rexglue-sdk)).

It is not an emulator running the game code: the game's PowerPC program is translated to C++ ahead of
time and compiled into a normal Windows executable. On top of that this project adds
a keyboard and mouse scheme with proper mouse look, an unlockable frame rate, SMAA and FSR
image processing, automatic quality selection and a number of fixes. See [docs/FEATURES.md](docs/FEATURES.md).

> **This repository and its releases contain no game data.** You need your own copy of
> Gears of War: Judgment for the Xbox 360. Nothing here is affiliated with or endorsed by
> Microsoft, Epic Games or People Can Fly.

**Status: early release.** The single-player campaign boots and plays, but testing so far has
mostly covered the early part of it. Multiplayer and online features are not supported. Please read
[Known issues](#known-issues) before reporting a problem.

## Download and play

1. Open the **[Releases](../../releases)** page of this repository and download the latest
   `GearsOfWarJudgment-PC-<version>.zip`.
2. Unzip it into a folder you can write to (not `Program Files`). It contains two programs:
   - **`GearsOfWarJudgment.exe`**: the game, one self-contained file.
   - **`Graphics Settings.exe`**: a settings window; keep it next to the game.
3. Have your game files ready: the folder from your own disc that contains `default.xex` and the
   `GearGame` folder. Only the retail version is supported (title ID `4D530A26`).
4. Double-click `GearsOfWarJudgment.exe`. The first time, a window asks for that game folder. It is
   remembered, so you are only asked once. (If you put the game exe inside the game folder, or the
   game folder next to it, nothing is asked.)

That is all: no installer, nothing else to download, no extra runtime. The first start takes a few
seconds longer because the exe unpacks its files into `%LOCALAPPDATA%\GearsOfWarJudgmentPC\bin`.
Your settings, saves and logs are kept in the folder you put the exe in.

To change graphics settings before playing, start **`Graphics Settings.exe`**. It has quality level,
internal resolution, texture filtering, anti-aliasing, FSR, frame limit, mouse sensitivity and more,
and a **Play** button. The same settings are in the game under **F4**.

### Requirements

- Windows 10 or 11, 64-bit
- A DirectX 12 graphics card. The quality is chosen from your card automatically
  (see [Quality](docs/FEATURES.md#automatic-quality)). It was developed and tested on an
  NVIDIA RTX 5070 Ti and **nothing else**; AMD, Intel and handheld GPUs use the same code but are
  untested.
- 8 GB of RAM or more (the game itself uses about 3 to 4 GB). The game files are roughly 7 GB; the
  cache adds a few hundred MB.
- A reasonably fast CPU. The game is limited by the processor, not the graphics card (see
  [Known issues](#known-issues)).

## What is in it

| | |
|---|---|
| **Keyboard and mouse** | Full keyboard and mouse controls with raw mouse look: no smoothing, no acceleration, no "sliding", frame-rate independent. Menus work with the mouse. Xbox and other controllers work too. |
| **Frame limiter and unlock** | Default 60 fps with evenly spaced frames. Set **Frame limit: Unlocked** in the settings window for a fully unlocked frame rate (it also turns vsync off). **F8** switches the limiter off and on in game. The game's own 62 fps cap is lifted when unlocked. |
| **FPS counter** | **F9** shows fps, frame time, 1% low and the current quality level. |
| **Settings menu** | **F4** opens the settings menu: every option below can be changed in game and is saved. |
| **SMAA + FSR** | SMAA anti-aliasing and AMD FSR 1.0 upscaling/sharpening of the final image, each switchable. |
| **Automatic quality** | Low / Medium / High chosen from your GPU, lowered for the next launch if 60 fps is not held. |
| **Shader and pipeline cache** | Compiled graphics pipelines are remembered on disk, so each area stutters only the first time. |
| **Cleaner image** | Film grain, motion blur and the game's own FXAA are off by default; community resolution-scaling fix applied. |
| **Saves** | Checkpoint saving and loading across restarts works. |

Everything is explained in detail, with every setting name, in **[docs/FEATURES.md](docs/FEATURES.md)**.

## Controls

The game was made for a gamepad, so every keyboard and mouse input below drives one gamepad button
or stick. A controller still works too (Xbox pads, and anything SDL recognises).

| Gamepad | Keyboard / mouse | What it does in the game |
|---|---|---|
| **Left stick** | **W A S D** | Move; move along cover |
| **Right stick** | **Mouse movement** | Look and aim |
| **Right trigger (RT)** | **Left mouse button** | Fire |
| **Left trigger (LT)** | **Right mouse button** | Aim down the sights (hold) |
| **A** | **Space**, **E** or **Enter** | The context button: take cover, interact, use, pick up weapons and ammo, revive a teammate; **Enter** also accepts in menus |
| **B** | **Ctrl** (hold), **V** | Crouch (hold Ctrl); execute a downed enemy (V) |
| **X** | **R** | Reload; hold to rev the chainsaw |
| **Y** | **Mouse wheel** (scroll either way) | Swap between your weapons |
| **Left bumper (LB)** | **Q** | Spot and mark enemies and objects |
| **Right bumper (RB)** | **F** | Melee; vault over cover |
| **Left stick click (L3)** | **Z** | Walk (slow movement) |
| **Right stick click (R3)** | not assigned | |
| **D-pad** | **Arrow keys** | Menu navigation |
| **Start** | **Escape** | Pause menu |
| **Back** | **Tab** | Objectives and scoreboard |
| **Guide** | not assigned | |

Not available on the keyboard because the console game has no gamepad button for them: weapon
slots 1 to 4, the scope toggle (middle mouse), push-to-talk and chat. Weapon swapping is the
mouse wheel only.

**In menus** the game has no cursor, so the mouse drives the selection: scroll the **wheel** to move
up and down, **move the mouse** to step in that direction, **left click** to accept (A) and
**right click** to go back (B). **Enter** accepts and **Escape** is Start.

Every key above can be changed in the F4 menu or in `config/gowj.toml`, for example
`keybind_x = "R"` or `keybind_a = "Space,E,Return"` (several keys can share one button).
The setting names are `keybind_a`, `keybind_b`, `keybind_x`, `keybind_y`,
`keybind_left_shoulder`, `keybind_right_shoulder`, `keybind_left_trigger`, `keybind_right_trigger`,
`keybind_lstick_press`, `keybind_rstick_press`, `keybind_lstick_up` (and down, left, right),
`keybind_dpad_up` (and down, left, right), `keybind_start`, `keybind_back`.

### Keys added by this project

| Key | What it does |
|---|---|
| **F4** | Settings menu. The backtick key (`` ` ``) opens the console |
| **F8** | Frame limiter: switch between unlocked and the limit (60 fps by default) |
| **F9** | FPS counter on/off |
| **F11 / F12** | Mouse sensitivity down / up by 20% (the new value is written to the log) |
| **Insert** or **F1** | Release / capture the mouse pointer |
| F3 | Performance overlay |

## Known issues

- **Only tested on one PC** (Ryzen 7 5800X3D, RTX 5070 Ti, Windows 11). AMD and Intel graphics and
  handheld PCs are untested. They should pick lower quality settings automatically, but expect
  rough edges and please report what you see.
- **CPU limited.** About 90 to 110 fps unlocked on a Ryzen 7 5800X3D; a slower CPU gives less.
- **For an unlocked frame rate, set Frame limit to Unlocked in Graphics Settings.exe.** F8 in game
  does not turn off vsync, so it may stay at half your monitor's refresh rate.
- **Unlocked can stutter or tear** without a G-Sync or FreeSync monitor. Use the 60 fps limit if so.
- **Short freezes the first time you enter an area.** Graphics pipelines are compiled on first use
  (a fraction of a second, occasionally a little more). They are saved, so the same area is
  smooth afterwards. The cache lives in `%LOCALAPPDATA%\GearsOfWarJudgmentPC\cache` and can be
  several hundred MB.
- **Small hitches in busy scenes**, about one frame in 200.
- **Single-player campaign only.** Multiplayer and Xbox Live features are not available.
- **Menus have no mouse cursor.** The game was not designed for one; the mouse moves the selection.
- **The settings menu (F4) pauses SMAA/FSR while it is open**, so the picture looks softer until you close it.
- Thin, distant geometry (antennas, wires) can shimmer, because there is no temporal anti-aliasing.
- Internal resolution above 2x (2560x1440) is not supported and breaks the picture.
- Windows only.

## Building from source

See **[BUILDING.md](BUILDING.md)** for the full steps. In short, you provide two things:

- **`default.xex`** from your own copy of the game (the retail Xbox 360 disc version, title ID
  `4D530A26`), placed at `game/default.xex` in the repository folder
- the **[ReXGlue SDK](https://github.com/rexglue/rexglue-sdk)** (Windows x64, version 0.10.0.9-dev)

then run `scripts\build.ps1 -Sdk <sdk folder>`. The recompiled game code is generated on your machine
from your `default.xex` and is not part of this repository.

## Reporting problems

Open an issue and attach the newest file from the `logs` folder next to the program (and
`gowj_crash.log` if there is one). Say which graphics card and Windows version you use.

## License

The source code in this repository is under the BSD 3-Clause license ([LICENSE](LICENSE)). Third-party
components keep their own licenses ([THIRD_PARTY_NOTICES.txt](THIRD_PARTY_NOTICES.txt)). The license does not
cover the game itself, which belongs to its owners and is not included.

## Credits

- [ReXGlue SDK](https://github.com/rexglue/rexglue-sdk), the runtime this builds on, which is derived
  from the [Xenia](https://github.com/xenia-project/xenia) project.
- [XenonRecomp](https://github.com/hedge-dev/XenonRecomp) for the idea and the recompilation approach.
- AMD FidelityFX Super Resolution 1.0 and SMAA (Jimenez et al.), see
  [THIRD_PARTY_NOTICES.txt](THIRD_PARTY_NOTICES.txt).
- The xenia-canary community patch set for this title (film grain, FXAA and resolution-scaling
  fixes), re-implemented here as switchable settings.
