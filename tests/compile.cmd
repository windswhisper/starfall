@echo off
setlocal EnableDelayedExpansion
cd /d "%~dp0.."
call tools\msvc-x86.cmd || exit /b 1
call tools\module-sources.cmd
set "shared=build\tests\shared"
if /i "%~1"=="lua" (
    call tools\build-lua.cmd "%shared%\lua"
    exit /b
)
if /i "%~1"=="resources" (
    rc /nologo /fo "%shared%\research.res" assets\research\ui.rc
    exit /b
)
if /i "%~1"=="modules" (
    cl /nologo /c /std:c++20 /EHsc /O2 /MT /MP /utf-8 /DSYR_VER=2 /DNOMINMAX /D_CRT_SECURE_NO_WARNINGS /Ireferences\Phobos\YRpp /Fo%shared%\modules\ %starfallModules% || exit /b 1
    set "moduleObjects="
    for %%s in (%starfallModules%) do set moduleObjects=!moduleObjects! "%shared%\modules\%%~ns.obj"
    lib /nologo /out:"%shared%\modules.lib" !moduleObjects!
    exit /b
)
if /i "%~1"=="ui" (
    cl /nologo /c /std:c++20 /EHsc /O2 /MT /utf-8 /DSYR_VER=2 /DNOMINMAX /D_CRT_SECURE_NO_WARNINGS /Ireferences\Phobos\YRpp /Fo%shared%\ResearchUI.obj src\ResearchUI.cpp
    exit /b
)
if /i "%~1"=="core" (
    cl /nologo /c /std:c++20 /EHsc /O2 /MT /utf-8 /DSYR_VER=2 /DNOMINMAX /D_CRT_SECURE_NO_WARNINGS /Ireferences\Phobos\YRpp /Fo%shared%\Starfall.obj src\Starfall.cpp
    exit /b
)
if /i "%~1"=="game" (
    set "out=build\tests\%~2"
    goto game
)
if /i "%~1"=="unit" goto unit
exit /b 2
:game
set "other=%shared%\ResearchUI.obj"
if /i "%~2"=="hex-icons" set "other=%shared%\Starfall.obj"
@rem Retain hook-only modules as well as modules referenced by C++ symbols.
cl /nologo /std:c++20 /EHsc /O2 /MT /LD /utf-8 /DSYR_VER=2 /DNOMINMAX /D_CRT_SECURE_NO_WARNINGS /Isrc /Ireferences\Phobos\YRpp /Fo%out%\ /Fe%out%\Starfall.dll tests\game\%~2\Probe.cpp "%other%" "%shared%\modules.lib" "%shared%\lua\lua.lib" "%shared%\research.res" /link /WHOLEARCHIVE:"%shared%\modules.lib" /INCREMENTAL:NO /OPT:REF /OPT:NOICF
exit /b %errorlevel%
:unit
set "out=build\tests\unit"
set "extra="
if /i "%~2"=="ResearchModelTest" set "extra=src\Research.cpp src\ConfigText.cpp"
if /i "%~2"=="ActionArgumentsTest" set "extra=%shared%\ResearchUI.obj %shared%\modules.lib"
cl /nologo /std:c++20 /EHsc /O2 /MT /utf-8 /DSYR_VER=2 /DNOMINMAX /D_CRT_SECURE_NO_WARNINGS /Isrc /Ireferences\Phobos\YRpp /Fo%out%\ /Fe%out%\%~2.exe tests\unit\%~2.cpp %extra% "%shared%\lua\lua.lib" "%shared%\research.res" /link /INCREMENTAL:NO /OPT:REF /OPT:NOICF
exit /b %errorlevel%
