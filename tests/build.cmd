@echo off
setlocal
cd /d "%~dp0.."
set "otherSource=src\ResearchUI.cpp"
if /i "%~1"=="hex-icons" (
    set "otherSource=src\Starfall.cpp"
    goto compile
)
if /i "%~1"=="projectiles" goto compile
if /i "%~1"=="script-grants" goto compile
if /i "%~1"=="weapon-range" goto compile
if /i "%~1"=="fire-rate" goto compile
if /i "%~1"=="before-fire" goto compile
if /i "%~1"=="before-damage" goto compile
if /i "%~1"=="aura" goto compile
if /i "%~1"=="power-state" goto compile
if /i "%~1"=="condition-formula" goto compile
if /i "%~1"=="passenger-consume" goto compile
if /i "%~1"=="promotion" goto compile
if /i "%~1"=="transport-support" goto compile
if /i "%~1"=="signal" goto compile
if /i "%~1"=="deployed-state" goto compile
if /i "%~1"=="immune" goto compile
if /i "%~1"=="spawn-passives" goto compile
if /i "%~1"=="research-cost" goto compile
if /i "%~1"=="max-health" goto compile
if /i "%~1"=="passive-gained" goto compile
if /i "%~1"=="reapply" goto compile
if /i "%~1"=="passive-stacks" goto compile
if /i "%~1"=="passive-animations" goto compile
if /i "%~1"=="conversion" goto compile
if /i "%~1"=="research-hud" goto compile
if /i "%~1"=="health-formula" goto compile
echo Usage: tests\build.cmd projectiles ^| script-grants ^| weapon-range ^| fire-rate ^| before-fire ^| before-damage ^| aura ^| power-state ^| condition-formula ^| passenger-consume ^| deployed-state ^| immune ^| health-formula ^| research-hud ^| hex-icons ^| max-health ^| passive-gained ^| reapply ^| passive-stacks ^| passive-animations ^| conversion ^| research-cost ^| spawn-passives
exit /b 2
:compile
call tools\msvc-x86.cmd || exit /b 1
set "out=build\tests\%~1"
if not exist "%out%" mkdir "%out%" || exit /b 1
rc /nologo /fo "%out%\research.res" assets\research\ui.rc || exit /b 1
cl /nologo /std:c++20 /EHsc /O2 /MT /LD /utf-8 /DSYR_VER=2 /DNOMINMAX /D_CRT_SECURE_NO_WARNINGS /Isrc /Ireferences\Phobos\YRpp /Fo%out%\ /Fe%out%\Starfall.dll tests\game\%~1\Probe.cpp %otherSource% src\VisualScale.cpp "%out%\research.res" /link /INCREMENTAL:NO /OPT:REF /OPT:NOICF
exit /b %errorlevel%
