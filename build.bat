@echo off
REM Script to build RastaConverter on Windows systems
REM Usage: build.bat [DEBUG|RELEASE] [GUI|NOGUI] [x64|x86] [THREAD_DEBUG] [NO_IMPROVEMENT_LOGS] [UI_DEBUG] [IGNORE_SDL_QUIT]

REM Default values
set BUILD_TYPE=Release
set GUI_OPTION=
set ARCH=x64
set GENERATOR="Visual Studio 17 2022"
REM Default debug flags are OFF unless explicitly enabled via args
set THREAD_DEBUG_FLAG=-DTHREAD_DEBUG=OFF
set SUPPRESS_IMPROVEMENT_LOGS_FLAG=-DSUPPRESS_IMPROVEMENT_LOGS=OFF
set UI_DEBUG_FLAG=-DUI_DEBUG=OFF
set IGNORE_SDL_QUIT_FLAG=-DIGNORE_SDL_QUIT=OFF

REM Determine repository root (directory of this script)
set ROOT_DIR=%~dp0
set CONFIG_FILE=%ROOT_DIR%config.env

REM Parse command line arguments
for %%a in (%*) do (
    if /i "%%a"=="DEBUG" (
        set BUILD_TYPE=Debug
    ) else if /i "%%a"=="RELEASE" (
        set BUILD_TYPE=Release
    ) else if /i "%%a"=="GUI" (
        set GUI_OPTION=
    ) else if /i "%%a"=="NOGUI" (
        set GUI_OPTION=-DNO_GUI=ON
    ) else if /i "%%a"=="x64" (
        set ARCH=x64
    ) else if /i "%%a"=="x86" (
        set ARCH=Win32
    ) else if /i "%%a"=="THREAD_DEBUG" (
        set THREAD_DEBUG_FLAG=-DTHREAD_DEBUG=ON
    ) else if /i "%%a"=="NO_IMPROVEMENT_LOGS" (
        set SUPPRESS_IMPROVEMENT_LOGS_FLAG=-DSUPPRESS_IMPROVEMENT_LOGS=ON
    ) else if /i "%%a"=="UI_DEBUG" (
        set UI_DEBUG_FLAG=-DUI_DEBUG=ON
    ) else if /i "%%a"=="IGNORE_SDL_QUIT" (
        set IGNORE_SDL_QUIT_FLAG=-DIGNORE_SDL_QUIT=ON
    )
)

REM Create build directory name
set BUILD_DIR=build-%BUILD_TYPE%
if "%GUI_OPTION%"=="" (
    set BUILD_DIR=%BUILD_DIR%-GUI
) else (
    set BUILD_DIR=%BUILD_DIR%-NoGUI
)
set BUILD_DIR=%BUILD_DIR%-%ARCH%

REM Check for required dependencies
where cmake >nul 2>&1
if %ERRORLEVEL% NEQ 0 (
    echo CMake is required but not installed. Aborting.
    exit /b 1
)

REM Load configuration from config.env at the repository root if present
if exist "%CONFIG_FILE%" (
    for /f "usebackq tokens=1,* delims==" %%a in ("%CONFIG_FILE%") do (
        if /I "%%a"=="FREEIMAGE_DIR" set FREEIMAGE_DIR=%%b
        if /I "%%a"=="SDL2_DIR" set SDL2_DIR=%%b
        if /I "%%a"=="SDL2_TTF_DIR" set SDL2_TTF_DIR=%%b
    )
)

REM Create build directory
echo Creating build directory: %BUILD_DIR%
if not exist %BUILD_DIR% mkdir %BUILD_DIR%
cd %BUILD_DIR%

REM Use paths from environment variables or prompt user to enter them
if not defined FREEIMAGE_DIR (
    if not defined FREEIMAGE_DIR (
        echo FREEIMAGE_DIR environment variable not set.
        set /p FREEIMAGE_DIR="Enter path to FreeImage library: "
        if not exist "%CONFIG_FILE%" (
            echo # RastaConverter build configuration > "%CONFIG_FILE%"
        )
        echo FREEIMAGE_DIR=%FREEIMAGE_DIR%>> "%CONFIG_FILE%"
    )
)

if not defined SDL2_DIR (
    if not defined SDL2_DIR (
        echo SDL2_DIR environment variable not set.
        set /p SDL2_DIR="Enter path to SDL2 library: "
        if not exist "%CONFIG_FILE%" (
            echo # RastaConverter build configuration > "%CONFIG_FILE%"
        )
        echo SDL2_DIR=%SDL2_DIR%>> "%CONFIG_FILE%"
    )
)

if not defined SDL2_TTF_DIR (
    if not defined SDL2_TTF_DIR (
        echo SDL2_TTF_DIR environment variable not set.
        set /p SDL2_TTF_DIR="Enter path to SDL2_ttf library: "
        if not exist "%CONFIG_FILE%" (
            echo # RastaConverter build configuration > "%CONFIG_FILE%"
        )
        echo SDL2_TTF_DIR=%SDL2_TTF_DIR%>> "%CONFIG_FILE%"
    )
)

REM Configure and build
echo Configuring with CMake...
cmake .. -G %GENERATOR% -A %ARCH% -DCMAKE_BUILD_TYPE=%BUILD_TYPE% %GUI_OPTION% %THREAD_DEBUG_FLAG% %SUPPRESS_IMPROVEMENT_LOGS_FLAG% %UI_DEBUG_FLAG% %IGNORE_SDL_QUIT_FLAG% -DFREEIMAGE_DIR=%FREEIMAGE_DIR% -DSDL2_DIR=%SDL2_DIR% -DSDL2_TTF_DIR=%SDL2_TTF_DIR%

echo Building...
cmake --build . --config %BUILD_TYPE%

REM Check if build was successful
if %ERRORLEVEL% EQU 0 (
    echo Build successful! Executable is located at: %BUILD_DIR%\%BUILD_TYPE%\rasta.exe
) else (
    echo Build failed!
    exit /b 1
)
