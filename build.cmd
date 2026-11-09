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
set "STARFALL_VS="
for /f "usebackq delims=" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "STARFALL_VS=%%i"
if not defined STARFALL_VS (
    echo Install Visual Studio 2022 with Desktop development with C++, MSVC v143 and a Windows SDK.
    exit /b 1
)
call "%STARFALL_VS%\VC\Auxiliary\Build\vcvars32.bat" >nul || exit /b 1
if not exist build mkdir build
cl /nologo /std:c++20 /EHsc /O2 /MT /LD /utf-8 /DSYR_VER=2 /DNOMINMAX /D_CRT_SECURE_NO_WARNINGS /Ireferences\Phobos\YRpp /Fobuild\ /Febuild\Starfall.dll src\Starfall.cpp src\ResearchUI.cpp /link /INCREMENTAL:NO /OPT:REF /OPT:NOICF
exit /b %errorlevel%
