@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Professional\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64
cl /nologo /std:c++20 /EHsc Tests\performance_diagnostics_test.cpp /Fe:Tests\performance_diagnostics_test.exe /Fo:Tests\performance_diagnostics_test.obj /link d3d11.lib
if errorlevel 1 exit /b 1
Tests\performance_diagnostics_test.exe
