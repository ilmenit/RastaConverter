@echo off
REM Script to set up environment variables for building RastaConverter

set /p FREEIMAGE_DIR="Enter path to FreeImage library: "
set /p SDL2_DIR="Enter path to SDL2 library: "
set /p SDL2_TTF_DIR="Enter path to SDL2_ttf library: "

REM Save to config.env for build scripts
echo FREEIMAGE_DIR=%FREEIMAGE_DIR%> config.env
echo SDL2_DIR=%SDL2_DIR%>> config.env
echo SDL2_TTF_DIR=%SDL2_TTF_DIR%>> config.env

REM Set environment variables for current session
setx FREEIMAGE_DIR "%FREEIMAGE_DIR%"
setx SDL2_DIR "%SDL2_DIR%"
setx SDL2_TTF_DIR "%SDL2_TTF_DIR%"

echo Environment variables set successfully.
echo You can now build RastaConverter with any of the configurations.
