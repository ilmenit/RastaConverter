@echo off
setlocal ENABLEDELAYEDEXPANSION

echo === RastaConverter Build (Windows) ===

REM Honor debug_build variable for verbose command echoing
if /I "!debug_build!"=="1" (
    echo [debug] Enabling command echo
    @echo on
)

REM Check if CMake is available
cmake --version >nul 2>&1
if %errorlevel% neq 0 (
    echo [error] CMake not found in PATH. Please install CMake 3.21+ and add it to PATH.
    exit /b 1
)

REM Defaults
set PRESET=x64-release
set CONFIG=Release
set CHECK_DEPS=0
set BUILD_NO_GUI=0
set CLEAN=0
set CLEANONLY=0
set EXTRA_CMAKE_ARGS=
set COMPILER=

REM Parse arguments
:parse_args
if "%~1"=="" goto args_done
if /I "%~1"=="debug" ( set PRESET=x64-debug & set CONFIG=Debug & shift & goto parse_args )
if /I "%~1"=="release" ( set PRESET=x64-release & set CONFIG=Release & shift & goto parse_args )
if /I "%~1"=="x86" (
    if /I "%CONFIG%"=="Debug" ( set PRESET=win32-debug ) else ( set PRESET=win32-release )
    shift & goto parse_args
)
if /I "%~1"=="x64" (
    if /I "%CONFIG%"=="Debug" ( set PRESET=x64-debug ) else ( set PRESET=x64-release )
    shift & goto parse_args
)
if /I "%~1"=="ninja" (
    if /I "%CONFIG%"=="Debug" ( set PRESET=ninja-debug ) else ( set PRESET=ninja-release )
    shift & goto parse_args
)
REM Optional compiler selector (simple tokens)
if /I "%~1"=="msvc" ( set COMPILER=msvc & shift & goto parse_args )
if /I "%~1"=="clang" ( set COMPILER=clang & shift & goto parse_args )
if /I "%~1"=="clang-cl" ( set COMPILER=clang-cl & shift & goto parse_args )
if /I "%~1"=="gcc" ( set COMPILER=gcc & shift & goto parse_args )
if /I "%~1"=="mingw" ( set COMPILER=gcc & shift & goto parse_args )
if /I "%~1"=="icx" ( set COMPILER=icx & shift & goto parse_args )
if /I "%~1"=="nogui" ( set BUILD_NO_GUI=1 & shift & goto parse_args )
if /I "%~1"=="check" ( set CHECK_DEPS=1 & shift & goto parse_args )
if /I "%~1"=="CLEAN" ( set CLEAN=1 & shift & goto parse_args )
if /I "%~1"=="CLEANONLY" ( set CLEANONLY=1 & shift & goto parse_args )

REM Accumulate extra -D options and other passthrough args
set "ARG=%~1"
if /I "!ARG:~0,2!"=="-D" (
    set "EXTRA_CMAKE_ARGS=!EXTRA_CMAKE_ARGS! ^"%~1^""
) else (
    set "EXTRA_CMAKE_ARGS=!EXTRA_CMAKE_ARGS! ^"%~1^""
)
shift
goto parse_args

:args_done

REM If a non-MSVC compiler was requested, prefer Ninja presets automatically
if defined COMPILER (
    if /I not "%COMPILER%"=="msvc" (
        if /I "%CONFIG%"=="Debug" (
            set PRESET=ninja-debug
        ) else (
            set PRESET=ninja-release
        )
    )
)

REM Map COMPILER token to CMake compiler variables
if /I "%COMPILER%"=="clang" (
    set "EXTRA_CMAKE_ARGS=%EXTRA_CMAKE_ARGS% -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++"
)
if /I "%COMPILER%"=="clang-cl" (
    set "EXTRA_CMAKE_ARGS=%EXTRA_CMAKE_ARGS% -DCMAKE_C_COMPILER=clang-cl -DCMAKE_CXX_COMPILER=clang-cl"
)
if /I "%COMPILER%"=="gcc" (
    set "EXTRA_CMAKE_ARGS=%EXTRA_CMAKE_ARGS% -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++"
)
if /I "%COMPILER%"=="icx" (
    set "EXTRA_CMAKE_ARGS=%EXTRA_CMAKE_ARGS% -DCMAKE_C_COMPILER=icx -DCMAKE_CXX_COMPILER=icx"
)

if %CHECK_DEPS% EQU 1 (
    echo [info] Checking dependencies...
    cmake -P check_dependencies.cmake
    echo.
)

echo [info] Preset: %PRESET%
echo [info] Config: %CONFIG%
if %BUILD_NO_GUI% EQU 1 (
    echo [info] Building GUI + Console
) else (
    echo [info] Building GUI only
)

REM Configure
set CFG_ARGS=--preset %PRESET%
if %BUILD_NO_GUI% EQU 1 set CFG_ARGS=%CFG_ARGS% -DBUILD_NO_GUI=ON
if not "%EXTRA_CMAKE_ARGS%"=="" set CFG_ARGS=%CFG_ARGS% %EXTRA_CMAKE_ARGS%

if %CLEAN% EQU 1 (
    set BINARY_DIR=build\%PRESET%
    if exist "%BINARY_DIR%" (
        echo [info] CLEAN requested: removing %BINARY_DIR%
        rmdir /s /q "%BINARY_DIR%"
    )
)

echo [info] Configuring project...
REM Pin source dir to avoid stray args being interpreted as a source path
cmake -S . %CFG_ARGS%
if %errorlevel% neq 0 (
    echo [error] Configuration failed.
    echo [hint] Try one of the following:
    echo   - Provide paths in config.env: FREEIMAGE_DIR, SDL2_DIR, SDL2_TTF_DIR
    echo   - OR install system packages:
    echo       Ubuntu:   sudo apt install libfreeimage-dev libsdl2-dev libsdl2-ttf-dev
    echo       macOS:    brew install freeimage sdl2 sdl2_ttf
    echo       Windows:  use vcpkg or vendor SDKs
    echo   - With vcpkg: set VCPKG_ROOT then pass toolchain, e.g.:
    echo       cmake --preset %PRESET% -DCMAKE_TOOLCHAIN_FILE="%%VCPKG_ROOT%%\scripts\buildsystems\vcpkg.cmake"
    echo   - You can run: build.bat check   to see discovery hints
    exit /b 1
)

if %CLEANONLY% EQU 1 goto end

echo [info] Building project...
cmake --build --preset %PRESET% --config %CONFIG%
if %errorlevel% neq 0 (
    echo [error] Build failed. See errors above.
    exit /b 1
)

echo.
echo [success] Build completed successfully.
echo [hint] Artifacts are in build\%PRESET%\%CONFIG%\
if %BUILD_NO_GUI% EQU 1 (
    echo [hint] Console binary: build\%PRESET%\%CONFIG%-NO_GUI\RastaConverter-NO_GUI.exe
)

goto end

:show_help
echo Usage: build.bat [options]
echo   debug ^| release           Choose configuration (default: release)
echo   x86 ^| x64 ^| ninja        Choose generator/arch preset
echo   msvc ^| clang ^| clang-cl ^| gcc ^| icx  Optional compiler selector
echo   nogui                      Also build console version
echo   check                      Check dependencies
echo   CLEAN ^| CLEANONLY         Clean build directory
echo   -DVAR=VALUE ...            Extra CMake cache options
echo.
echo Tips:
echo   - Provide FREEIMAGE_DIR/SDL2_DIR/SDL2_TTF_DIR in config.env or env vars to hint discovery
echo   - Selecting non-MSVC compiler auto-uses Ninja preset; ensure compilers are on PATH
echo   - Or install via vcpkg/homebrew/apt and add the toolchain when desired
echo   - Set debug_build=1 for verbose script debug

:end
endlocal


