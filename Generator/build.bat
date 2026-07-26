@echo off
setlocal
cd /d "%~dp0"
del /q output.xex 2>nul
"%~dp0mads.exe" no_name.asq -o:output.xex > "!log.txt"
if errorlevel 1 exit /b %errorlevel%
echo Generated: %~dp0output.xex
if exist output.xex start "" output.xex
if exist output.png start "" output.png
