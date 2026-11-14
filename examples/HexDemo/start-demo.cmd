@echo off
setlocal
cd /d "%~dp0..\..\build\hex-demo\runtime"
if not exist Syringe.exe (
  echo Demo runtime is missing: build\hex-demo\runtime
  pause
  exit /b 1
)
if not exist Starfall.dll exit /b 1
if exist Starfall.HexDemo.test del /q Starfall.HexDemo.test
type nul > Starfall.HexDemo.enabled
start "" Syringe.exe -SPAWN gamemd.exe -i=Starfall.dll -i=Ares.dll -i=cncnet5.dll -i=Phobos.dll --args="-SPAWN -LOG -CD -WIN" --nodetach
