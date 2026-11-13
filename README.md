# ZoidsAssaultRecomp – Zoids Assault (Xbox 360) static recompilation

Unofficial, work-in-progress static recompilation of **Zoids Assault** (Xbox 360,
USA, title ID 415407D1) to native Windows x64 using
[ReXGlue](https://github.com/rexglue/rexglue-sdk).

**This repository contains no game files.** You need your own original disc
(or an ISO you dumped from it). The game executable is not released: it
contains code translated from the game, so everyone builds it from their own copy.
The only program attached to a release is `zoidssetup-v0.0.2.exe`, the setup installer.

## Quick start

### Prerequisites
- Windows 10 or 11 (64-bit) and a graphics card with Direct3D 12 (current drivers).
- **Your own** Zoids Assault disc (USA): an ISO or a folder you already
  extracted that contains `default.xex`. See [Getting your game files](#getting-your-game-files).
- About 15 GB of free disk space and an internet connection (the build tools are downloaded).
- A controller (tested: Xbox Series controller).

### Install
1. Download **[`zoidssetup-v0.0.2.exe`](https://github.com/LomoXSoft/ZoidsAssaultRecomp/releases/download/v0.0.2/zoidssetup-v0.0.2.exe)**
   from the [releases page](https://github.com/LomoXSoft/ZoidsAssaultRecomp/releases).
   That file is the setup program, not the game. It builds the game on your PC from your disc.
2. Run `zoidssetup-v0.0.2.exe`. Choose the language (English only for now, like the game), then **Install**.
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
Preview (0.0.2). Tested on one PC (Windows 11, AMD Radeon RX 9060 XT).
- Boots, plays the intro and story cutscenes with sound, menus and gameplay.
- Played with an Xbox Series controller from a new game to the end of the
  first mission.
- Later missions, saving and loading have not been tested yet.
- No launcher or settings yet: it runs at the game's original resolution and
  frame rate.
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
  - LLVM/Clang (`clang++`; tested with 23.1), CMake ≥ 3.25, Ninja, Git.
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

## Repository layout
- `app/` – ReXGlue project: manifest (function boundaries, setjmp/longjmp),
  per-game fixes in `src/hooks.cpp`, crash logger in `src/crash_diag.cpp`.
- `tools/` – build/run scripts, `xiso_extract.py`, and the setup: `zoidssetup.cpp`
  (window), `install.ps1` (the steps it runs), `build_setup.bat` (builds the setup exe).
- `tools/artwork/` – Lomorsin pixel art for the setup window (not game art). The game window and
  shortcut use the game's own icon, read from your `default.xex` when it runs.
- `checks/` – checks for the setup logic and packaging.

## Plans
- Play through the remaining missions and fix what breaks.
- A launcher with resolution, window mode and frame-rate options.

## License and legal notice
The code in this repository is licensed under the **GNU GPL v3.0** (see
`LICENSE`).

This is a fan project, not affiliated with or endorsed by Takara Tomy, Atlus,
Microsoft or any rights holder. "Zoids" and related names are trademarks of
their respective owners. No game code, assets or data are distributed here;
you must own the original game.
