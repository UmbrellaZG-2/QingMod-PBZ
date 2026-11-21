@echo off
setlocal
call "D:\VScode\Root\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64
if errorlevel 1 exit /b %errorlevel%
cd /d "%~dp0"
"deps\build_tools\cmake\data\bin\cmake.exe" -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_MAKE_PROGRAM="%~dp0deps\build_tools\bin\ninja.exe"
if errorlevel 1 exit /b %errorlevel%
"deps\build_tools\cmake\data\bin\cmake.exe" --build build -j 4
exit /b %errorlevel%
