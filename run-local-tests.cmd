@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Professional\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64
set "CXX=C:\Program Files\Microsoft Visual Studio\18\Professional\VC\Tools\Llvm\x64\bin\clang++.exe"
"C:\Users\103va\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe" Tests\run_interop_test.py
if errorlevel 1 exit /b 1
"C:\Users\103va\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe" Tests\run_performance_test.py
if errorlevel 1 exit /b 1
"C:\Users\103va\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe" Tests\run_enable_test.py
if errorlevel 1 exit /b 1
