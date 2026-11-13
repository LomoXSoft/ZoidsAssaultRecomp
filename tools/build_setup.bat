@echo off
rem Build tools\zoidssetup-v<app\VERSION>.exe. This is the setup program, not the game.
rem It does not upload or replace a GitHub release asset.
rem The embedded source is "git archive HEAD": commit first, uncommitted changes are not packed.
rem Requires clang++ with the MSVC libraries (the portable toolchain under <root>\tools,
rem or LLVM plus Visual Studio Build Tools), llvm-rc, git and Python 3.
setlocal
cd /d "%~dp0"
call "%~dp0env_win.bat" || exit /b 1
where llvm-rc >nul 2>&1 || (echo llvm-rc was not found. & exit /b 1)
where git >nul 2>&1 || (echo git was not found. & exit /b 1)
where python >nul 2>&1 || (echo python was not found. & exit /b 1)
set "VER="
for /f "usebackq delims=" %%V in ("%~dp0..\app\VERSION") do if not defined VER set "VER=%%V"
if not defined VER (echo app\VERSION is empty. & exit /b 1)
set "OUT=zoidssetup-v%VER%.exe"
python make_setup_art.py || exit /b 1
llvm-rc /nologo /FO zoidssetup.res zoidssetup.rc || exit /b 1
clang++ -std=c++17 -O2 -Wall -Wextra -Werror -DUNICODE -D_UNICODE -DWINVER=0x0A00 -D_WIN32_WINNT=0x0A00 -fms-runtime-lib=static -o "%OUT%" zoidssetup.cpp zoidssetup.res -Wl,/SUBSYSTEM:WINDOWS -Wl,/ENTRY:wWinMainCRTStartup -lcomdlg32 -lshell32 -lole32 -luuid -lgdi32 -lcomctl32 -luser32 -ladvapi32 || exit /b 1
set "SNAP=%TEMP%\zoidssetup-src-%VER%"
if exist "%SNAP%" rmdir /s /q "%SNAP%"
mkdir "%SNAP%" || exit /b 1
git -C "%~dp0.." archive --format=tar HEAD | tar -x -C "%SNAP%" || exit /b 1
python pack_setup_payload.py --exe "%OUT%" --repo "%SNAP%" --overlay "%SNAP%" || exit /b 1
rmdir /s /q "%SNAP%"
for %%F in ("%OUT%") do echo built %~dp0%OUT% (%%~zF bytes)
certutil -hashfile "%OUT%" SHA256 | findstr /v ":"
