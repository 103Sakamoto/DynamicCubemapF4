@echo off
setlocal
cd /d "%~dp0"
call Source\BUILD.cmd
exit /b %errorlevel%
