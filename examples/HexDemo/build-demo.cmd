@echo off
setlocal
cd /d "%~dp0..\.."
call tools\msvc-x86.cmd || exit /b 1
if not exist build\hex-demo mkdir build\hex-demo
call tools\build-lua.cmd build\hex-demo\lua || exit /b 1
rc /nologo /fo build\hex-demo\research.res assets\research\ui.rc || exit /b 1
cl /nologo /std:c++20 /EHsc /O2 /MT /LD /utf-8 /DSYR_VER=2 /DNOMINMAX /D_CRT_SECURE_NO_WARNINGS /Isrc /Ireferences\Phobos\YRpp /Fobuild\hex-demo\ /Febuild\hex-demo\Starfall.dll examples\HexDemo\Demo.cpp src\ResearchUI.cpp src\VisualScale.cpp build\hex-demo\lua\lua.lib build\hex-demo\research.res /link /IMPLIB:build\hex-demo\Starfall.lib /INCREMENTAL:NO /OPT:REF /OPT:NOICF
exit /b %errorlevel%
