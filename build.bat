@echo off
setlocal
if "%GEODE_SDK%"=="" (
  echo GEODE_SDK is not set.
  exit /b 1
)
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
if errorlevel 1 exit /b 1
cmake --build build --config Release
