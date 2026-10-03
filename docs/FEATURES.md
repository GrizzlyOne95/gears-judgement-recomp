# Features

Everything this project adds on top of the recompiled game, what it does, and which setting
controls it. Settings can be changed in game with **F4**, or by editing `config/gowj.toml`
(created on first run from `config/gowj.default.toml`; delete it to return to the defaults).

- [FPS counter (F9)](#fps-counter-f9)
- [Frame rate: limiter and unlock (F8)](#frame-rate-limiter-and-unlock-f8)
- [Settings menu (F4)](#settings-menu-f4)
- [Keyboard and mouse](#keyboard-and-mouse)
- [Image quality: SMAA and FSR](#image-quality-smaa-and-fsr)
- [Automatic quality](#automatic-quality)
- [Shader and pipeline cache](#shader-and-pipeline-cache)
- [Cleaner picture](#cleaner-picture)
- [Saves](#saves)
- [Other behaviour](#other-behaviour)
- [Advanced switches](#advanced-switches)

---

## FPS counter (F9)

Press **F9** to show or hide a small counter in the top-left corner of the game window. It is
drawn by a separate transparent window, so it does not touch the game's rendering, ignores mouse
clicks, and only shows while the game is the window in front.

| Line | Meaning |
|---|---|
| `101 FPS  9.9 ms` | Frames per second and average frame time over the last second (frames the game actually rendered). |
| `1% low 58   worst 31 ms` | The frame rate of the slowest 1% of frames over the last 10 seconds, and the longest single frame in the last second. This is the number that tells you about stutter. |
| `present 101/s   limit 60 [F8]` | How many images per second are sent to the display, and the current frame limit (`UNLOCKED` when the limit is off). |
| `quality: high (auto)` | The quality level in use and why. If the game could not hold 60 fps for a while, this line turns into a warning that quality will be lowered on the next launch. |

The counter is on by default. To remove it completely, set the environment variable `GOWJ_FPS=0`.

## Frame rate: limiter and unlock (F8)

Out of the box the game is held at **60 fps with evenly spaced frames**. Press **F8** at any
time to switch to **unlocked** and press it again to return to the limit. The counter's third line shows
which one is active.

Why a limiter at all: on a high-refresh display, a game running at an uneven 100 fps feels worse than a
steady 60, because frames arrive 3, 4 or 5 refreshes apart. The limiter releases each frame on an
exact schedule. With a variable refresh rate (G-Sync / FreeSync) monitor an unlocked frame rate is
smooth, so use that: set `d3d12_allow_variable_refresh_rate_and_tearing = true` and press F8.

The original game also has its own hard cap of 62 fps built into the engine. When the limiter is off, that
cap is raised as well (the engine's `MaxSmoothedFrameRate` setting is found in memory and rewritten
a few times while the game runs). Without that, "unlocked" would still stop at 62.

| Setting | Default | What it does |
|---|---|---|
| `vsync` | `true` | Waits for the display refresh. The settings window sets this to `false` when the frame limit is Unlocked; with it on, an unlocked game can be held to about half the refresh rate. Takes effect at start. |
| `gowj_pace_hz` | `60` | Frame limit in frames per second. `0` = unlocked. Any number is a cap (`30`, `90`, `120`...). F8 toggles between `0` and this value without a restart. |
| `gowj_present_limit` | `true` | Skips re-sending an unchanged image to the display. The game otherwise re-presents the same frame several times, each costing a full-resolution pass. |
| `gowj_pace_lead_us` | `3000` | How long before the display's refresh the limiter releases a frame (microseconds). Leave it. |
| `gowj_pace_refresh_hz` | `0` | Display refresh used by the limiter. `0` = read it from the primary display. |
| `vsync` | `true` | Vertical sync. |
| `d3d12_allow_variable_refresh_rate_and_tearing` | `false` | Allows variable refresh rate. Turn on with an unlocked frame rate on a G-Sync / FreeSync monitor. |

The unlock can be disabled with the environment variable `GOWJ_FPSUNLOCK=0` (the limiter and F8 still work, but
the engine's own 62 fps cap then stays).

## Settings menu (F4)

**F4** opens the settings menu in game (the backtick key opens the console, F3 a performance
overlay). It lists the game's settings grouped by category. Changes are saved to
`config/gowj.toml`, so they survive a restart. A few settings are only read when the graphics
system starts (internal resolution, render path, texture-cache sizes); change those and restart.

While the menu is open the SMAA/FSR pass is paused so the menu stays readable, which makes the picture look softer
until you close it.

What you will find there that this project added, by category:

**gfx** (picture)

| Setting | Default | |
|---|---|---|
| `gowj_quality` | `auto` | Quality level: `auto`, `low`, `medium`, `high`. See [Automatic quality](#automatic-quality). |
| `gowj_smaa` | on (not on `low`) | SMAA 1x anti-aliasing. |
| `gowj_fsr` | `true` | The whole final-image pass (SMAA + FSR). Off = the plain stretch the runtime does itself. |
| `gowj_fsr_easu` | `true` | FSR edge-adaptive upscaling to your screen. Off = bilinear stretch. |
| `gowj_fsr_sharpness` | `0.5` | FSR sharpening in stops: `0` sharpest, `2` softest. |
| `gowj_film_grain` | `false` | The game's animated film grain. Off removes a lot of the "shimmer". |
| `gowj_motion_blur` | `false` | The game's motion blur. |
| `gowj_game_fxaa` | `false` | The game's own FXAA pass. Off is sharper; SMAA replaces it. |
| `gowj_scaling_fix` | `true` | The community resolution-scaling fix for the post-process chain. |

**perf** (frame rate): the settings in the table in the previous section.

**mnk** (keyboard and mouse): see the next section.

**GPU / Display**: standard graphics settings of the runtime that matter most:
`draw_resolution_scale_x` and `_y` (internal resolution, 1 or 2), `anisotropic_override`
(0 to 5, texture filtering up to 16x), `native_2x_msaa`, the `texture_cache_memory_limit_*`
sizes, `render_target_path_d3d12`, `fullscreen`, `present_dither`.

## Keyboard and mouse

The game was made for a gamepad. This build has a complete keyboard and mouse scheme (controls are in
the [README](../README.md#controls)), and controllers still work (Xbox pads and anything SDL knows).

**Mouse look.** The mouse is not turned into a fake thumbstick. Its movement is written straight into the game's look
input, so the camera turns by exactly the distance you move the mouse:

- no smoothing: the game's built-in thumbstick low-pass filter is removed
- no acceleration: the game's stick-acceleration ramp is removed, so turning is 1:1
- no speed limit: a fast flick is not capped at the maximum speed a thumbstick could turn
- independent of frame rate: the same hand movement turns the same angle at 60 or 200 fps
- no "sliding" after you stop moving the mouse

| Setting | Default | |
|---|---|---|
| `mnk_raw_look_scale` | `0.003` | Sensitivity. Higher turns farther per mouse pixel. **F11 / F12** lower / raise it by 20% while playing; the new value is written to the log, copy it into your config to keep it. |
| `mnk_yaw_scale` | `1.0` | Horizontal multiplier. |
| `mnk_pitch_scale` | `1.5` | Vertical multiplier. |
| `mnk_invert_y` | `false` | Invert vertical look. |
| `mnk_raw_look` | `true` | The direct-angle look above. Off = the old emulated thumbstick (rate limited, console feel). |
| `mnk_guest_smoothing` | `false` | On brings back the game's thumbstick low-pass filter. |
| `mnk_view_accel` | `false` | On keeps the game's turn acceleration. |
| `mnk_uncapped_look` | `true` | Allows flicks faster than a thumbstick's maximum turn rate. |
| `mnk_mouse` | `true` | Use the mouse for looking. |
| `mnk_mode` | `true` | Keyboard and mouse scheme on/off. |

**Pointer.** The pointer is captured by default and confined to the window. **Insert** or **F1**
releases or recaptures it; it is released automatically when the window loses focus.

**Menus.** The game's menus have no cursor, so the mouse drives the selection: the **wheel** scrolls,
**moving the mouse** steps up/down/left/right (`mnk_menu_mouse_step`, default 120 pixels per step),
**left click** accepts and **right click** goes back (`mnk_menu_mouse_nav`).

**Rebinding.** The keys are listed under the Input categories in F4 (`keybind_*`). Add a line such as
`keybind_a = "Space,E"` to `config/gowj.toml` to rebind.

## Image quality: SMAA and FSR

The game renders at 1280x720 (or 2560x1440 on the High quality level). A final pass on the graphics
card then improves that image before it reaches your screen:

1. **SMAA 1x** finds edges and smooths them. The original game's anti-aliasing is weak, and this
   works well on the sharp, high-contrast edges of this game.
2. **FSR 1.0 EASU** upscales the result to your screen's resolution, preserving edges far better
   than a plain stretch.
3. **FSR RCAS** sharpens the upscaled image. `gowj_fsr_sharpness` sets how much (`0` sharpest,
   `2` softest, default `0.5`).

All three can be switched off separately (`gowj_smaa`, `gowj_fsr_easu`, `gowj_fsr`). The pass costs about
a millisecond or less on a desktop card. It is skipped while the F4 menu or the console is open.

## Automatic quality

Every time the game starts it looks at your graphics card and picks a level:

| Level | Internal resolution | SMAA | Texture filtering | Chosen when |
|---|---|---|---|---|
| **low** | 1280x720 | off | 4x | integrated or handheld GPU, or under 3.5 GB of video memory |
| **medium** | 1280x720 | on | 8x | a graphics card with under 11 GB of video memory, or any non-NVIDIA card |
| **high** | 2560x1440 | on | 16x | an NVIDIA card with 12 GB or more, on a 1080p-or-larger screen |

Texture-cache size and the number of background pipeline-compile threads also follow the level and
your hardware (video memory budget and CPU core count). FSR upscaling is on at every level.
Only NVIDIA hardware has been measured; AMD and Intel cards are capped at **medium** unless you ask
for more, because the **high** path has not been verified on them.

**It lowers itself when needed.** If, after the first 75 seconds, the game stays below 60 fps (the typical
frame takes over 18.5 ms, or more than 1 frame in 20 takes over 20 ms) for three checks in a row
(90 seconds), the **next** launch uses one level lower, and the fourth line of the FPS counter tells you.
It never raises the level by itself. The decision is remembered in
`%LOCALAPPDATA%\GearsOfWarJudgmentPC\user\autotune.ini` (delete that file to reset it).

**To choose yourself:** set `gowj_quality = "low"`, `"medium"` or `"high"` in `config/gowj.toml`
(or F4). Any individual setting you add to `config/gowj.toml` (for example
`draw_resolution_scale_x`, `anisotropic_override`, `native_2x_msaa`, `gowj_smaa`) overrides the level
for that one setting, and automatic lowering is then off for the resolution you set. The log line starting
with `QUALITY` says what was detected and why.

## Shader and pipeline cache

The graphics card has to compile a pipeline for every combination of shaders and states the game
uses, and each can take a few hundred milliseconds, which shows up as a freeze the first time you see something new.

This project keeps a **pipeline library** on disk. The first time a pipeline is needed it is
compiled and stored; every later time (including after restarting the game) it is loaded in about a
millisecond. So an area stutters at most once. The files are in
`%LOCALAPPDATA%\GearsOfWarJudgmentPC\cache` (a pipeline library of a few hundred MB plus the runtime's shader
store). They are safe to delete; they are rebuilt as you play. They are specific to your GPU and driver,
so a driver update starts them over. `GOWJ_PSOLIB=0` turns the pipeline library off.

## Cleaner picture

Defaults chosen for a sharper, steadier image than the original:

- **Film grain off** (`gowj_film_grain`): per-frame noise was a large part of the shimmering.
- **Motion blur off** (`gowj_motion_blur`): the radial speed blur and the camera blur are removed.
- **Game FXAA off** (`gowj_game_fxaa`): replaced by SMAA.
- **Resolution-scaling fix on** (`gowj_scaling_fix`): the post-process chain stays correct above 720p.
- **Dithering** (`present_dither`): hides banding in smoke, sky and dark walls.
- **Alpha-test fix** (`use_fuzzy_alpha_epsilon`): stops alpha-tested edges flickering on NVIDIA.

## Saves

Checkpoint saves work across restarts. The recompiled game's storage checks needed several fixes (it
re-rolled storage aliases between sessions and created empty slot files, which made it report your
progress as corrupted). Saves are kept in the `saves` folder next to the program.

## Other behaviour

- **Graphics card choice.** On a PC with more than one graphics card, the one with the most video memory is used
  (set `d3d12_adapter = <index>` to choose another).
- **Power throttling.** The program opts out of Windows' power and timer throttling, which on handhelds and
  laptops with efficiency cores can otherwise park the game on slow cores.
- **Logs.** A log per run is written to `logs`. Include the newest one in a bug report. If the program
  crashes, `gowj_crash.log` is written next to it.
- **Game folder.** The first run asks for your game folder and remembers it in `gowj_game_path.txt`.

## Advanced switches

Environment variables (set before starting the program):

| Variable | Effect |
|---|---|
| `GOWJ_FPS=0` | Remove the FPS counter. |
| `GOWJ_FPSUNLOCK=0` | Do not raise the engine's own 62 fps cap. |
| `GOWJ_PSOLIB=0` | Turn the pipeline library off. |
| `GOWJ_QUALITY=low\|medium\|high` | Force a quality level for this run. |

Any setting can also be given on the command line as `--setting=value`, which overrides the config
file.
