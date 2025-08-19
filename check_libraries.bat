@echo off
setlocal

echo Checking SDL2_ttf Library Contents
echo ==================================

REM Read config.env from root if present, else fallback default
if exist "config.env" (
    for /f "tokens=1,2 delims==" %%a in (config.env) do (
        if "%%a"=="SDL2_TTF_DIR" set SDL2_TTF_DIR=%%b
    )
) else (
    set SDL2_TTF_DIR=d:\Projekty\SDL2_ttf
)

echo SDL2_TTF_DIR = %SDL2_TTF_DIR%
echo.

set SDL2_TTF_LIB=%SDL2_TTF_DIR%\lib\x64\SDL2_ttf.lib

if exist "%SDL2_TTF_LIB%" (
    echo ✓ Found SDL2_ttf.lib: %SDL2_TTF_LIB%
    echo.
    echo Checking if library contains TTF symbols...
    
    REM Use dumpbin to check symbols in the library
    dumpbin /symbols "%SDL2_TTF_LIB%" 2>nul | findstr /i "TTF_Init TTF_OpenFont TTF_RenderText"
    if %errorlevel% equ 0 (
        echo ✓ TTF symbols found in library
    ) else (
        echo ✗ TTF symbols NOT found in library
        echo.
        echo This might be an import library. Checking for DLL...
        set SDL2_TTF_DLL=%SDL2_TTF_DIR%\lib\x64\SDL2_ttf.dll
        if exist "%SDL2_TTF_DLL%" (
            echo ✓ Found SDL2_ttf.dll: %SDL2_TTF_DLL%
            echo This is normal - .lib is import library, .dll contains actual code
        ) else (
            echo ✗ SDL2_ttf.dll not found
        )
    )
) else (
    echo ✗ SDL2_ttf.lib not found: %SDL2_TTF_LIB%
)

echo.
echo Also checking for typical dependency setup... (freetype is handled by your package manager/toolchain)
echo.

REM Check what's in the lib directory
echo Contents of %SDL2_TTF_DIR%\lib\x64\:
if exist "%SDL2_TTF_DIR%\lib\x64\" (
    dir "%SDL2_TTF_DIR%\lib\x64\" /b
) else (
    echo Directory not found
)

echo.
pause


