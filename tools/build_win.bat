@echo off
rem Build ZoidsAssaultRecomp on Windows from your own game files.
rem usage: tools\build_win.bat   (run from the repo folder)
rem Expected layout (see README): <root>\ZoidsAssaultRecomp (this repo),
rem <root>\sdk\win-amd64 (ReXGlue SDK 0.10.1), <root>\gamedata (your extracted
rem disc), optional <root>\tools (portable toolchain, as the setup installs it).
setlocal
set R=%~dp0..\..
call "%~dp0env_win.bat" || exit /b 1
set "REXBIN=%R%\sdk\win-amd64\bin"
if not exist "%REXBIN%\rexglue.exe" (echo sdk\win-amd64\bin\rexglue.exe not found, see README & exit /b 1)
cd /d "%~dp0..\app"
if not exist assets mkdir assets
copy /y "%R%\gamedata\default.xex" assets\default.xex >nul || (echo gamedata\default.xex not found & exit /b 1)
rem First build: run the code generator once so CMake sees the generated sources.
rem Later builds rerun it from the Ninja rule only when an input changed.
rem Keep the flags the same as that rule (none), or every build redoes it.
if exist generated\default\sources.cmake goto compile
echo ZOIDSPHASE codegen
echo Running code generation, this takes a few minutes...
rem rexglue imports the MSVC C runtime. With the portable MSVC, copy those DLLs
rem beside rexglue so an older vcruntime earlier on PATH (Python's) is not used.
if defined VCToolsInstallDir if exist "%VCToolsInstallDir%bin\Hostx64\x64\vcruntime140.dll" (
  for %%F in (vcruntime140.dll vcruntime140_1.dll msvcp140.dll) do if exist "%VCToolsInstallDir%bin\Hostx64\x64\%%F" if not exist "%REXBIN%\%%F" copy /y "%VCToolsInstallDir%bin\Hostx64\x64\%%F" "%REXBIN%\%%F" >nul
)
rem The setup writes its own codegen_win.log from this output, so use another name.
set "CODELOG=%R%\codegen_rexglue.txt"
"%REXBIN%\rexglue.exe" codegen zoidsassault_manifest.toml > "%CODELOG%" 2>&1
rem Not RC: CMake reads the environment variable RC as the resource compiler.
set GEN_EXIT=%ERRORLEVEL%
rem rexglue's last line has no line break: end it, or the next marker joins it.
if exist "%CODELOG%" type "%CODELOG%"
echo.
if not "%GEN_EXIT%"=="0" (
  echo CODEGEN FAILED exit %GEN_EXIT%, see codegen_rexglue.txt
  exit /b %GEN_EXIT%
)
:compile
echo ZOIDSPHASE compile
rem An RC variable holding an exit code (0) makes CMake take "0" as the resource
rem compiler and store it in the cache. Drop only that cache, then clear RC.
set "BCACHE=%~dp0..\app\out\build\win-amd64-release"
set "CF=%BCACHE%\CMakeCache.txt"
if exist "%CF%" (
  findstr /R /C:"^CMAKE_RC_COMPILER:[A-Za-z]*=0$" "%CF%" >nul
  if not errorlevel 1 (
    echo Removing CMake cache that stored RC=0 as the resource compiler.
    del /q "%CF%"
    if exist "%BCACHE%\CMakeFiles" rmdir /s /q "%BCACHE%\CMakeFiles"
  )
)
if "%RC%"=="0" set "RC="
cmake --preset win-amd64-release -DCMAKE_PREFIX_PATH="%R%\sdk\win-amd64" -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ || exit /b 1
cmake --build out\build\win-amd64-release -j %NUMBER_OF_PROCESSORS% || exit /b 1
echo BUILD OK
