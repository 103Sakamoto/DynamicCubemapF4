@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Professional\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64
if not exist Tests\bin mkdir Tests\bin
for %%T in (array_guard encoding invalid_reference matching publication recovery reference_commit) do (
 cl /nologo /std:c++20 /EHsc Tests\%%T_test.cpp /Fe:Tests\bin\%%T_test.exe /Fo:Tests\bin\%%T_test.obj
 if errorlevel 1 exit /b 1
 Tests\bin\%%T_test.exe
 if errorlevel 1 exit /b 1
)
