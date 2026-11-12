@echo off
setlocal
cd /d "%~dp0"
if not exist "references\Phobos\YRpp\TechnoClass.h" (
    git submodule update --init -- references/Phobos/YRpp
    if errorlevel 1 (
        echo Could not fetch YRpp. Install Git and build from a Git clone.
        exit /b 1
    )
)
call tools\msvc-x86.cmd || exit /b 1
if not exist build\obj mkdir build\obj || exit /b 1
rc /nologo /fo build\obj\research.res assets\research\ui.rc || exit /b 1
cl /nologo /std:c++20 /EHsc /O2 /MT /LD /utf-8 /DSYR_VER=2 /DNOMINMAX /D_CRT_SECURE_NO_WARNINGS /Ireferences\Phobos\YRpp /Fobuild\obj\ /Febuild\Starfall.dll src\Starfall.cpp src\ResearchUI.cpp src\VisualScale.cpp build\obj\research.res /link /IMPLIB:build\obj\Starfall.lib /INCREMENTAL:NO /OPT:REF /OPT:NOICF
exit /b %errorlevel%
