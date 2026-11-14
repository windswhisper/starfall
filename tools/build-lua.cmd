@echo off
setlocal
set "luaOut=%~1"
if not exist "%luaOut%" mkdir "%luaOut%" || exit /b 1
cl /nologo /c /TP /EHsc /O2 /MT /MP /utf-8 /D_CRT_SECURE_NO_WARNINGS /Fo%luaOut%\ references\lua\*.c || exit /b 1
lib /nologo /out:"%luaOut%\lua.lib" "%luaOut%\*.obj"
exit /b %errorlevel%
