@echo off
setlocal
cd /d "%~dp0"
if not defined DCF4_PYTHON set "DCF4_PYTHON=python"
"%DCF4_PYTHON%" tools\package_release.py
exit /b %errorlevel%
