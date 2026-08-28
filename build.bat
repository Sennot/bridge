@echo off
setlocal

if "%GEODE_SDK%"=="" (
    echo GEODE_SDK is not set.
    echo Example: set GEODE_SDK=D:\dev\geode
    exit /b 1
)

cmake -S . -B build -G "Visual Studio 17 2022" -A x64
if errorlevel 1 exit /b 1

cmake --build build --config Release
if errorlevel 1 exit /b 1

endlocal
