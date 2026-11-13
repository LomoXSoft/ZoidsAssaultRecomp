@echo off
rem Launch Zoids Assault recomp (Windows). Logs to zoids.log in <root>.
set R=%~dp0..\..
cd /d "%~dp0..\app\out\build\win-amd64-release"
.\zoidsassault.exe --game_data_root="%R%\gamedata" --gpu_plugin=xenos --log_file="%R%\zoids.log" %*
