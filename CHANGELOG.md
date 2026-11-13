# Changelog

Newest first. Versions are in `app/VERSION`.

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
