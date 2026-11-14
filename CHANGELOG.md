# Changelog

Newest first. Versions are in `app/VERSION`.

## 0.1.0 – first milestone: the game works
- The game boots, plays its cutscenes with sound, and plays from a new game to the
  end of the first mission with a controller; saving and loading work.
  This release is the 0.0.4 pre-release under its milestone number.
- `recomplauncher.json` and a `ZoidsAssaultRecomp-v0.1.0-Windows-x64.zip` release file:
  RecompLauncher checks your ISO or extracted folder (with the Redump USA dump as the
  known good copy), runs the setup without a window, shows the display, resolution and
  VSync settings on the game's page, and starts the game.
- Setup: `--prepare --game <iso or folder> [--accept-msvc-license]` installs without a
  window and prints `progress` lines; `--play` starts the installed game and, after an
  update of the recomp, rebuilds it first. Without these options the setup is unchanged.
- Setup: the Redump dump of the USA disc is accepted as a verified ISO too.
- Game: started from RecompLauncher (`RECOMPLAUNCHER=1`), it does not show its own
  launcher window unless Shift is held or `--launcher` is given.
- README: "Get it on RecompLauncher" badge.

## 0.0.3 – launcher and display settings
- Launcher before the game starts (first run, after an update, Shift held, or
  `--launcher`): borderless / window / exclusive fullscreen, internal resolution
  1x / 2x / 3x, VSync. Saved in `zoids_settings.cfg` next to the exe.
- VSync off no longer makes the game run too fast: it is held at its own 30 FPS.
- **F3** shows the frame rate.
- The game window, taskbar and shortcuts use the game's own icon, read from your
  `default.xex` when the game runs (nothing from the game is in the repository).
  The setup writes `game\zoids_icon.ico` after the build for the shortcuts.
- Started without `Play.bat`, the game finds `<root>\gamedata` and writes
  `<root>\zoids.log` like a setup install.
- `zoids.log` records the game's save calls (`zoids save:` lines). Saves were
  checked: after a restart **Load** lists them.
- Setup: an update no longer copies settings or icon files from the build folder
  into `game`.
- 60 FPS was investigated and is not included: the game is frame-locked (see the
  README's Plans). Research flags for that work are off by default.

## 0.0.2 – one-click setup
- New `zoidssetup-v0.0.2.exe`: a graphical installer in English (the game's
  only language) that downloads a portable toolchain with pinned SHA-256 checksums,
  extracts your USA disc, builds the game and creates `game\Play.bat` and
  optional shortcuts. **Update** rebuilds an existing installation and keeps the
  game files, tools and saves. Same design as the AfroSamuraiRecomp setup.
- Pinned toolchain: LLVM/Clang 20.1.8, MSVC 14.51 with Windows SDK 28000, CMake,
  Ninja, MinGit, embeddable Python 3.12, ReXGlue SDK 0.10.1.
- Setup window and shortcut art: a Lomorsin seal zoid.
- `build_win.bat`: portable tools take priority, the C runtime is copied next to
  rexglue for code generation, and a CMake cache that stored `RC=0` is cleared.
- `checks/`: setup logic, packaging and update checks.

## 0.0.1 – first public preview (source only)
- Native Windows x64 build with ReXGlue SDK 0.10.1 and the D3D12 GPU plugin.
- Boots, plays cutscenes with sound, menus and gameplay; the first mission
  was played to the end with a controller.
- Fixes: `XamUserGetName` returns the Win32 "no such user" code the game
  expects for empty slots (it quit during start-up before); the game's
  setjmp/longjmp pair is registered, so an image decode error no longer
  corrupts the stack; a shutdown routine no longer writes through a null
  pointer when start-up fails; function boundaries the code generator missed.
- `tools/build_win.bat`, `tools/run_win.bat` and `tools/xiso_extract.py`.
- Crash logger: `crash_diag.log` with a stack trace when the game closes on
  an unhandled fault.
