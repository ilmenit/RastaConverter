@echo off
setlocal

echo Checking SDL3_ttf Library Contents
echo ==================================

REM Read config.env from root if present, else fallback default
if exist "config.env" (
    for /f "tokens=1,2 delims==" %%a in (config.env) do (
        if "%%a"=="SDL3_TTF_DIR" set SDL3_TTF_DIR=%%b
    )
) else (
    set SDL3_TTF_DIR=d:\Projekty\SDL3_ttf
)

echo SDL3_TTF_DIR = %SDL3_TTF_DIR%
echo.

set SDL3_TTF_LIB=%SDL3_TTF_DIR%\lib\x64\SDL3_ttf.lib

if exist "%SDL3_TTF_LIB%" (
    echo ✓ Found SDL3_ttf.lib: %SDL3_TTF_LIB%
    echo.
    echo Checking if library contains TTF symbols...
    
    REM Use dumpbin to check symbols in the library
    dumpbin /symbols "%SDL3_TTF_LIB%" 2>nul | findstr /i "TTF_Init TTF_OpenFont TTF_RenderText"
    if %errorlevel% equ 0 (
        echo ✓ TTF symbols found in library
    ) else (
        echo ✗ TTF symbols NOT found in library
        echo.
        echo This might be an import library. Checking for DLL...
        set SDL3_TTF_DLL=%SDL3_TTF_DIR%\lib\x64\SDL3_ttf.dll
        if exist "%SDL3_TTF_DLL%" (
            echo ✓ Found SDL3_ttf.dll: %SDL3_TTF_DLL%
            echo This is normal - .lib is import library, .dll contains actual code
        ) else (
            echo ✗ SDL3_ttf.dll not found
        )
    )
) else (
    echo ✗ SDL3_ttf.lib not found: %SDL3_TTF_LIB%
)

echo.
echo Also checking for typical dependency setup... (freetype is handled by your package manager/toolchain)
echo.

REM Check what's in the lib directory
echo Contents of %SDL3_TTF_DIR%\lib\x64\:
if exist "%SDL3_TTF_DIR%\lib\x64\" (
    dir "%SDL3_TTF_DIR%\lib\x64\" /b
) else (
    echo Directory not found
)

echo.
pause

