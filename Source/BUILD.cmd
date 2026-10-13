@echo off
setlocal

cd /d "%~dp0"
if not exist "xmake.lua" (
    echo ERROR: xmake.lua was not found beside this script.
    exit /b 1
)

where xmake >nul 2>nul
if errorlevel 1 (
    echo ERROR: xmake is not available in this Developer Command Prompt.
    exit /b 1
)

if not defined DCF4_DETOURS set "DCF4_DETOURS=E:\Coding\F4\vcpkg\installed\x64-windows-static-md"
if not exist "%DCF4_DETOURS%\include\detours\detours.h" (
    echo ERROR: Missing %DCF4_DETOURS%\include\detours\detours.h
    exit /b 1
)
if not exist "%DCF4_DETOURS%\lib\detours.lib" (
    echo ERROR: Missing %DCF4_DETOURS%\lib\detours.lib
    exit /b 1
)

if not defined DCF4_PYTHON set "DCF4_PYTHON=python"
"%DCF4_PYTHON%" ..\tools\compile_shaders.py
if errorlevel 1 exit /b 1

call xmake f -p windows -a x64 -m releasedbg --toolchain=msvc --detours_root="%DCF4_DETOURS%"
if errorlevel 1 exit /b %errorlevel%

call xmake build -r DynamicCubemapF4
if errorlevel 1 exit /b %errorlevel%
if not exist "build\windows\x64\releasedbg\DynamicCubemapF4.dll" exit /b 1

echo.
echo DynamicCubemapF4 1.5.0 build completed. DLL:
echo %CD%\build\windows\x64\releasedbg\DynamicCubemapF4.dll
exit /b 0
