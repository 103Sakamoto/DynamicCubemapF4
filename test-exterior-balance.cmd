@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Professional\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64
powershell -NoProfile -ExecutionPolicy Bypass -File Tests\ExtractExteriorBalance.ps1
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /EHsc Tests\exterior_balance_test.cpp /Fe:Tests\exterior_balance_test.exe /Fo:Tests\exterior_balance_test.obj
if errorlevel 1 exit /b 1
Tests\exterior_balance_test.exe
