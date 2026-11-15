# ZoidsAssaultRecomp – Zoids Assault (Xbox 360) static recompilation

<a href="https://recomplauncher.com"><picture><source media="(prefers-color-scheme: dark)" srcset="https://recomplauncher.com/assets/press/launcher/badge-light.svg"><img alt="Get it on RecompLauncher" src="https://recomplauncher.com/assets/press/launcher/badge-dark.svg" height="48"></picture></a>

Unofficial, work-in-progress static recompilation of **Zoids Assault** (Xbox 360,
USA, title ID 415407D1) to native Windows x64 using
[ReXGlue](https://github.com/rexglue/rexglue-sdk).

**This repository contains no game files.** You need your own original disc
(or an ISO you dumped from it). The game executable is not released: it
contains code translated from the game, so everyone builds it from their own copy.
The programs attached to a release are the setup installer, `zoidssetup-v0.1.0.exe`, and
`ZoidsAssaultRecomp-v0.1.0-Windows-x64.zip`, the same setup packed for
[RecompLauncher](#recomplauncher).

## Quick start

### Prerequisites
- Windows 10 or 11 (64-bit) and a graphics card with Direct3D 12 (current drivers).
- **Your own** Zoids Assault disc (USA): an ISO or a folder you already
  extracted that contains `default.xex`. See [Getting your game files](#getting-your-game-files).
- About 15 GB of free disk space and an internet connection (the build tools are downloaded).
- A controller (tested: Xbox Series controller).

### Install
1. Download **[`zoidssetup-v0.1.0.exe`](https://github.com/LomoXSoft/ZoidsAssaultRecomp/releases/download/v0.1.0/zoidssetup-v0.1.0.exe)**
   from the [releases page](https://github.com/LomoXSoft/ZoidsAssaultRecomp/releases).
   That file is the setup program, not the game. It builds the game on your PC from your disc.
2. Run `zoidssetup-v0.1.0.exe`. Choose the language (English only for now, like the game), then **Install**.
3. Choose a folder, then your ISO or the extracted game folder (`default.xex` must be inside it).
4. Accept the Microsoft Visual Studio Build Tools license (the box starts unchecked).
   Shortcuts are optional. You can keep the downloaded tools or delete them when setup
   finishes. Deleting them does not remove `game`, the extracted game, the logs, your
   ISO, or your saves.
5. The setup downloads a portable toolchain (Git, CMake, Ninja, Python, LLVM/Clang,
   the Microsoft C++ compiler and Windows SDK, the ReXGlue SDK), each checked against
   a pinned SHA-256, extracts your disc and builds the game. HDD installation can take
   a long time; SSD/NVMe recommended.
6. On success the window says **Installation finished** and shows **Play**. **Play**
   starts `game\Play.bat` in that folder, only when you click it. A shortcut, if you
   asked for one, also starts that `Play.bat`.
7. The first start shows the launcher (display mode, internal resolution, VSync). See
   [Launcher and options](#launcher-and-options).

Nothing is installed system-wide and no admin rights are needed. To uninstall,
delete the install folder and the shortcuts. Saves are not in that folder: they are in
`%USERPROFILE%\Documents\zoidsassault`.

### Update
Run a newer `zoidssetup-v*.exe`, choose **Update**, and select the existing installation
folder (the one that contains `game\Play.bat`). Close the game first. The extracted
game, the tools and your saves are reused; the program files are rebuilt and replaced
only after that build succeeds.

### If something goes wrong
- If the window says **Installation did not finish**, read the details and
  `install.log` in the install folder, then run the setup again: finished steps are skipped.
- The game log is `zoids.log` in the install folder. If the game closes on its own,
  `game\crash_diag.log` has the place it stopped.
- Open an issue with what happened and the logs. **Never attach game files** (ISO,
  extracted files).

## Contents
- [What it is](#what-it-is)
- [Status and limitations](#status-and-limitations)
- [Requirements](#requirements)
- [Getting your game files](#getting-your-game-files)
- [Building from source](#building-from-source)
- [Playing](#playing)
- [Launcher and options](#launcher-and-options)
- [Repository layout](#repository-layout)
- [Plans](#plans)
- [License and legal notice](#license-and-legal-notice)

## What it is
The game's PowerPC executable (`default.xex`) is translated ahead of time into
C++ by ReXGlue's code generator and compiled into a native Windows program. The
Xbox 360 kernel, GPU and audio are provided by the ReXGlue runtime (derived from
Xenia). This repository holds only the project configuration (function
boundaries, setjmp/longjmp addresses), a few per-game fixes and build scripts.

## Status and limitations
0.1.0, the first milestone: the game works. Tested on one PC (Windows 11, AMD Radeon RX 9060 XT).
- Boots, plays the intro and story cutscenes with sound, menus and gameplay.
- Played with an Xbox Series controller from a new game to the end of the
  first mission.
- Saving works, and **Load** lists the saves after the game is closed and started
  again. Later missions have not been played through yet.
- 30 FPS, the game's own rate. 60 FPS is not available: the game advances every
  part of itself by a fixed step per frame (see [Plans](#plans)).
- Only the USA disc has been tested (see below). Other versions may not work.
- Windows x64 only for now (D3D12).
- Expect bugs; saves and settings may change between versions.

## Requirements
- Windows 10/11 x64, a D3D12 GPU.
- Original Zoids Assault disc (USA) and a way to make an ISO of it.
- ~10 GB free disk space.
- Toolchain (on PATH, or portable copies under `<root>\tools`, see below):
  - Visual Studio 2022 Build Tools (MSVC + Windows SDK) – run the build from a
    "x64 Native Tools" prompt, or put a portable MSVC in `<root>\tools\msvc`
    with `setup_x64.bat`.
  - LLVM/Clang (`clang++`; tested with 20.1 and 23.1), CMake ≥ 3.25, Ninja, Git.
  - Python 3 (only to extract the disc).
- ReXGlue SDK **0.10.1** (prebuilt, `rexglue-sdk-0.10.1-win-amd64.zip`).

## Getting your game files
1. Make an ISO of your own disc (e.g. with a compatible drive and dumping tool).
   The tested ISO is 7,835,492,352 bytes, SHA-1
   `BEE0071B6C14096F42E16991AF75977999F268DD`; its `default.xex` has SHA-1
   `E96732CBF8EB2497473021B4F5DB27D5E6CF6093`.
2. Create a root folder, e.g. `C:\zoids`, and extract the whole disc into
   `C:\zoids\gamedata`:

       python tools\xiso_extract.py "Zoids Assault (USA).iso" C:\zoids\gamedata /

   `gamedata\default.xex` must exist afterwards.

Never share these files or anything generated from them.

## Building from source
Without the setup (for development). Folder layout (`<root>` = e.g. `C:\zoids`):

    <root>\ZoidsAssaultRecomp   this repository
    <root>\sdk\win-amd64        ReXGlue SDK 0.10.1 (bin\rexglue.exe inside)
    <root>\gamedata             your extracted disc
    <root>\tools                optional portable toolchain (msvc, llvm, git)

Steps:

    cd C:\zoids
    git clone https://github.com/LomoXSoft/ZoidsAssaultRecomp.git
    rem SDK: download rexglue-sdk-0.10.1-win-amd64.zip from
    rem https://github.com/rexglue/rexglue-sdk/releases/tag/v0.10.1
    rem and extract it so that C:\zoids\sdk\win-amd64\bin\rexglue.exe exists.
    cd ZoidsAssaultRecomp
    tools\build_win.bat

`build_win.bat` copies your `default.xex` into `app\assets` (gitignored), runs
the ReXGlue code generator once (a few minutes, ~65 MB of generated C++) and
compiles. The output is `app\out\build\win-amd64-release\zoidsassault.exe`.

To rebuild from scratch after updating, delete `app\generated\default` and
`app\out`.

## Playing

    tools\run_win.bat

Runs the game with `gamedata` from `<root>`, using the D3D12 GPU plugin, and
writes `<root>\zoids.log`. Any ReXGlue option can be added on the command line.

If the game closes on its own, `crash_diag.log` next to the exe has the place it
stopped; please attach it and `zoids.log` to an issue.

## RecompLauncher
[RecompLauncher](https://recomplauncher.com) can install and update this recomp for you.
It downloads `ZoidsAssaultRecomp-v<version>-Windows-x64.zip` from the releases, asks for
your ISO or extracted folder (it checks it, and shows when it matches the Redump USA disc),
and runs the same setup without a window: you tick the Microsoft Build Tools license there,
and the first setup takes as long as the normal one. The game's page in the launcher has the
display, resolution and VSync settings (the same `zoids_settings.cfg` as the launcher window
below), and **Play** starts the game. After an update of the recomp, the first **Play**
rebuilds the game (a small window shows the progress), then starts it.

Everything goes into a `ZoidsAssaultRecomp` folder inside the launcher's folder for the game,
with the same layout as a setup install. The setup does this with two options that only the
launcher uses: `zoidssetup.exe --prepare --game <iso or folder> [--accept-msvc-license]`
(prints `progress <0..1> <text>` lines) and `zoidssetup.exe --play [game options]`.
Started from RecompLauncher, the game does not open its own launcher window (hold Shift to
open it anyway).

### Recomp Achievements
The game's own 8 Xbox 360 achievements work through ReXGlue, which keeps your unlocks in
`Documents\zoidsassault\achievements\415407D1.toml` and shows its own toast. Started from
RecompLauncher, the game also reports them to
[Recomp Achievements](https://github.com/elripalda/RecompAchievements): the list with names
and gamerscore, each unlock as it happens (the launcher then shows the toast), and when a save
is loaded. Without RecompLauncher that library does nothing. `zoids.log` has a
`zoids achievements:` line for each achievement the game writes.

## Launcher and options
A small window before the game starts, shown on the first run, after each update,
when **Shift** is held while the game starts, or with `--launcher`. **Quit** closes it
without starting the game.

- **Display**: borderless fullscreen (default), window, or exclusive fullscreen.
- **Internal resolution**: 1x (1280x720, the original), 2x (2560x1440) or 3x
  (3840x2160). Higher values look sharper and need a faster GPU.
- **VSync**: on (default) or off. With VSync off the game is still held at its own
  30 FPS; without that it would run too fast.
- **Show this window at every start**: unticked, the launcher stays closed until
  Shift is held.

The choices are saved in `zoids_settings.cfg` next to `zoidsassault.exe`. A value
given on the command line wins, e.g. `Play.bat --resolution_scale=2`.

In the game, **F3** shows the frame rate. Options for bug reports and development:

- `--zoids_fps_log=true`: the frame rate in `zoids.log` every 4 seconds.
- `--zoids_save_log=false`: turns off the log of the game's save calls (on by
  default; lines start with `zoids save:`).
- `--zoids_fps_cap=N`: hold the game at N FPS.

## Repository layout
- `app/` – ReXGlue project: manifest (function boundaries, setjmp/longjmp),
  per-game fixes in `src/hooks.cpp`, crash logger in `src/crash_diag.cpp`,
  launcher in `src/launcher.cpp`, game icon in `src/game_icon.cpp`, frame cap and
  F3 stats in `src/frame_stats.cpp`, save log in `src/save_diag.cpp`, frame-rate
  research flags in `src/research.cpp`, Recomp Achievements bridge in
  `src/achievements.cpp`.
- `app/third_party/rl_achievements/` – the Recomp Achievements library (its own license,
  see below).
- `tools/` – build/run scripts, `xiso_extract.py`, and the setup: `zoidssetup.cpp`
  (window), `install.ps1` (the steps it runs), `build_setup.bat` (builds the setup exe).
- `tools/artwork/` – Lomorsin pixel art for the setup window (not game art). The game window and
  shortcut use the game's own icon, read from your `default.xex` when it runs.
- `checks/` – checks for the setup logic and packaging.

## Plans
- Play through the remaining missions and fix what breaks.
- 60 FPS stays out of reach for now. The main loop (`sub_82229A48`) calls each game
  system once per frame with no time step, and the renderer flips its buffers every
  frame, so running at 60 doubles the game's speed and showing frames twice makes
  the screen flicker. It would need every system's per-frame steps halved. The
  research flags `--zoids_scan_consts`, `--zoids_dump_image` (writes a local file
  derived from your disc: keep it private) and `--zoids_swap_stack` help with that.

## License and legal notice
The code in this repository is licensed under the **GNU GPL v3.0** (see
`LICENSE`), except `app/third_party/rl_achievements/`, the Recomp Achievements library,
which keeps its own license (its `LICENSE`; rcheevos and cJSON inside it are MIT).

This is a fan project, not affiliated with or endorsed by Takara Tomy, Atlus,
Microsoft or any rights holder. "Zoids" and related names are trademarks of
their respective owners. No game code, assets or data are distributed here;
you must own the original game.
