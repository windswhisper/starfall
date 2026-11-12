@echo off
set "STARFALL_VS="
for /f "usebackq delims=" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "STARFALL_VS=%%i"
if not defined STARFALL_VS (
    echo Install Visual Studio 2022 with Desktop development with C++, MSVC v143 and a Windows SDK.
    exit /b 1
)
call "%STARFALL_VS%\VC\Auxiliary\Build\vcvars32.bat" >nul || exit /b 1
exit /b 0
