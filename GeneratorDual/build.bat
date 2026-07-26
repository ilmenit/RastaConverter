@echo off
setlocal
cd /d "%~dp0"
del /q out_dual.xex 2>nul
"%~dp0mads.exe" no_name.asq -o:out_dual.xex > "!log.txt"
if errorlevel 1 exit /b %errorlevel%
echo Generated: %~dp0out_dual.xex
if exist out_dual.xex start "" out_dual.xex
if exist out_dual_blended.png start "" out_dual_blended.png
