@echo off
setlocal
set "ROOT=%~dp0"

where cmake.exe >nul 2>nul
if errorlevel 1 (
    echo CMake was not found in PATH.
    exit /b 1
)

where ninja.exe >nul 2>nul
if errorlevel 1 (
    echo Ninja was not found in PATH.
    exit /b 1
)

cmake.exe -S "%ROOT%." -B "%ROOT%build" -G Ninja -DCMAKE_BUILD_TYPE=Release
if errorlevel 1 exit /b %errorlevel%

cmake.exe --build "%ROOT%build" --config Release
exit /b %errorlevel%
