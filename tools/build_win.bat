@echo off
rem Build ZoidsAssaultRecomp on Windows from your own game files.
rem usage: tools\build_win.bat   (run from the repo folder)
rem Expected layout (see README): <root>\ZoidsAssaultRecomp (this repo),
rem <root>\sdk\win-amd64 (ReXGlue SDK 0.10.1), <root>\gamedata (your extracted
rem disc), optional <root>\tools (portable toolchain).
setlocal
set R=%~dp0..\..
call "%~dp0env_win.bat" || exit /b 1
if not exist "%R%\sdk\win-amd64\bin\rexglue.exe" (echo sdk\win-amd64\bin\rexglue.exe not found, see README & exit /b 1)
cd /d "%~dp0..\app"
if not exist assets mkdir assets
copy /y "%R%\gamedata\default.xex" assets\default.xex >nul || (echo gamedata\default.xex not found & exit /b 1)
rem First build: run the code generator once so CMake sees the generated sources.
rem Later builds rerun it from the Ninja rule only when an input changed.
rem Keep the flags the same as that rule (none), or every build redoes it.
if not exist generated\default\sources.cmake (
  echo Running code generation, this takes a few minutes...
  "%R%\sdk\win-amd64\bin\rexglue.exe" codegen zoidsassault_manifest.toml > "%R%\codegen_win.log" 2>&1 || (echo CODEGEN FAILED, see codegen_win.log & exit /b 1)
)
cmake --preset win-amd64-release -DCMAKE_PREFIX_PATH="%R%\sdk\win-amd64" -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ || exit /b 1
cmake --build out\build\win-amd64-release -j %NUMBER_OF_PROCESSORS% || exit /b 1
echo BUILD OK
