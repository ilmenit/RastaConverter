@echo off
REM Script to build RastaConverter on Windows systems
REM Usage: build.bat [PRESET] [CONFIG] [CLEAN] [extra cmake -D options]
REM Examples:
REM   build.bat win-clangcl Release -DTHREAD_DEBUG=ON -DUI_DEBUG=ON
REM   build.bat win-msvc Debug -DNO_GUI=ON
REM   build.bat win-msvc Release CLEAN

setlocal ENABLEDELAYEDEXPANSION

set PRESET=
set CONFIG=Release
set CLEAN=0
set CLEANONLY=0
set EXTRA_CMAKE_ARGS=
set SINGLE_CONFIG_GEN=0
set USE_VCPKG=0
set AUTO_VCPKG=%AUTO_VCPKG%
if "%AUTO_VCPKG%"=="" set AUTO_VCPKG=0
set DISABLE_VCPKG=%DISABLE_VCPKG%
if "%DISABLE_VCPKG%"=="" set DISABLE_VCPKG=0
set NONINTERACTIVE=%NONINTERACTIVE%
if "%NONINTERACTIVE%"=="" set NONINTERACTIVE=0

where cmake >nul 2>&1
if %ERRORLEVEL% NEQ 0 (
  echo CMake is required but not installed. Aborting.
  exit /b 1
)

if "%~1"=="" (
  set PRESET=win-msvc
  goto :skip_preset_shift
)
set PRESET=%~1

REM Detect if the second arg is a config
if /I "%~2"=="Debug" (
  set CONFIG=Debug
  shift
) else if /I "%~2"=="Release" (
  set CONFIG=Release
  shift
)

REM Shift past preset
shift

:skip_preset_shift

:parse_args
if "%~1"=="" goto :args_done
set SHIFT_TWICE=0
if /I "%~1"=="CLEAN" (
  set CLEAN=1
) else if /I "%~1"=="CLEANONLY" (
  set CLEANONLY=1
) else (
  set "ARG=%~1"
  set "PREFIX=!ARG:~0,2!"
  if /I "!PREFIX!"=="-D" (
    rem Join space-separated -D VAR VALUE into -DVAR=VALUE for CMake
    for /f "tokens=1,2 delims==" %%A in ("!ARG!") do set "AFTER=%%B"
    if not defined AFTER (
      if not "%~2"=="" (
        set "EXTRA_CMAKE_ARGS=!EXTRA_CMAKE_ARGS! !ARG!=%~2"
        set SHIFT_TWICE=1
      ) else (
        set "EXTRA_CMAKE_ARGS=!EXTRA_CMAKE_ARGS! !ARG!"
      )
    ) else (
      set "EXTRA_CMAKE_ARGS=!EXTRA_CMAKE_ARGS! !ARG!"
    )
  ) else (
    set "EXTRA_CMAKE_ARGS=!EXTRA_CMAKE_ARGS! \"%~1\""
  )
)
shift
if %SHIFT_TWICE% EQU 1 shift
goto :parse_args
:args_done


REM Detect single-config generators to pass CMAKE_BUILD_TYPE
REM Criteria: preset contains "-make" or is MinGW Makefiles presets
echo %PRESET% | findstr /I /C:"-make" >nul && set SINGLE_CONFIG_GEN=1
if /I "%PRESET%"=="win-mingw-gcc" set SINGLE_CONFIG_GEN=1
if /I "%PRESET%"=="win-mingw-gcc-nogui" set SINGLE_CONFIG_GEN=1

REM Prepare for optional vcpkg fallback later (do not enable by default)
if exist vcpkg.json (
  if /I "%DISABLE_VCPKG%"=="1" (
    rem vcpkg disabled explicitly
  ) else if /I "%AUTO_VCPKG%"=="1" (
    if not defined VCPKG_ROOT (
      if exist .vcpkg\scripts\buildsystems\vcpkg.cmake (
        set VCPKG_ROOT=%CD%\.vcpkg
      ) else (
        where git >nul 2>&1 && (
          echo Preparing local vcpkg under .vcpkg ...
          git clone --depth 1 https://github.com/microsoft/vcpkg.git .vcpkg
          if exist .vcpkg\bootstrap-vcpkg.bat (
            call .vcpkg\bootstrap-vcpkg.bat -disableMetrics
            set VCPKG_ROOT=%CD%\.vcpkg
          )
        )
      )
    )
    if defined VCPKG_ROOT (
      set USE_VCPKG=1
      set "EXTRA_CMAKE_ARGS=%EXTRA_CMAKE_ARGS% -DCMAKE_TOOLCHAIN_FILE=\"%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake\" -DVCPKG_FEATURE_FLAGS=manifests"
    )
  )
)

REM Explicit opt-in: USE_VCPKG=1 forces immediate use of vcpkg if available
if exist vcpkg.json if /I NOT "%DISABLE_VCPKG%"=="1" if /I "%USE_VCPKG%"=="1" (
  if not defined VCPKG_ROOT (
    if exist .vcpkg\scripts\buildsystems\vcpkg.cmake (
      set VCPKG_ROOT=%CD%\.vcpkg
    ) else (
      where git >nul 2>&1 && (
        echo Preparing local vcpkg under .vcpkg ...
        git clone --depth 1 https://github.com/microsoft/vcpkg.git .vcpkg
        if exist .vcpkg\bootstrap-vcpkg.bat (
          call .vcpkg\bootstrap-vcpkg.bat -disableMetrics
          set VCPKG_ROOT=%CD%\.vcpkg
        )
      )
    )
  )
  if defined VCPKG_ROOT (
    set USE_VCPKG=1
    set "EXTRA_CMAKE_ARGS=%EXTRA_CMAKE_ARGS% -DCMAKE_TOOLCHAIN_FILE=\"%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake\" -DVCPKG_FEATURE_FLAGS=manifests"
  ) else (
    echo USE_VCPKG=1 requested but VCPKG_ROOT not available; proceeding without vcpkg.
  )
)

REM Recompute binary dir after any preset auto-switching above
set BINARY_DIR=out\build\%PRESET%

if %SINGLE_CONFIG_GEN% EQU 1 (
  set "EXTRA_CMAKE_ARGS=%EXTRA_CMAKE_ARGS% -DCMAKE_BUILD_TYPE=%CONFIG%"
)

REM Detect CMake version to decide if presets are supported
for /f "tokens=3" %%v in ('cmake --version ^| findstr /R "^cmake version"') do set CMAKE_VER=%%v
for /f "tokens=1,2 delims=." %%a in ("%CMAKE_VER%") do (
  set CMAKE_MAJOR=%%a
  set CMAKE_MINOR=%%b
)
set SUPPORTS_PRESETS=0
if %CMAKE_MAJOR% GEQ 4 set SUPPORTS_PRESETS=1
if %CMAKE_MAJOR% EQU 3 if %CMAKE_MINOR% GEQ 21 set SUPPORTS_PRESETS=1

set CONFIGURE_FAILED=0

if %SUPPORTS_PRESETS% EQU 1 (
  echo Configuring with CMake preset %PRESET% ...
  if not "%EXTRA_CMAKE_ARGS%"=="" echo Extra CMake args:%EXTRA_CMAKE_ARGS%
  cmake --preset %PRESET% %EXTRA_CMAKE_ARGS%
  if %ERRORLEVEL% NEQ 0 (
    echo Configure failed with presets, attempting a clean configure by removing %BINARY_DIR% ...
    if exist "%BINARY_DIR%" rmdir /s /q "%BINARY_DIR%"
    cmake --preset %PRESET% %EXTRA_CMAKE_ARGS%
  )
  if %ERRORLEVEL% NEQ 0 set CONFIGURE_FAILED=1
) else (
  echo CMake %CMAKE_MAJOR%.%CMAKE_MINOR% does not support presets. Falling back to -S/-B.
  set GEN=
  if /I "%PRESET%"=="win-msvc" set GEN=Visual Studio 17 2022
  if /I "%PRESET%"=="win-msvc-nogui" set GEN=Visual Studio 17 2022
  if /I "%PRESET%"=="win-msvc-ninja" set GEN=Ninja Multi-Config
  if /I "%PRESET%"=="win-msvc-ninja-nogui" set GEN=Ninja Multi-Config
  if /I "%PRESET%"=="win-clangcl" set GEN=Visual Studio 17 2022
  if /I "%PRESET%"=="win-clangcl-nogui" set GEN=Visual Studio 17 2022
  if /I "%PRESET%"=="win-mingw-gcc" set GEN=MinGW Makefiles
  if /I "%PRESET%"=="win-mingw-gcc-nogui" set GEN=MinGW Makefiles
  if "%GEN%"=="" set GEN=Visual Studio 17 2022
  set CFG_ARGS=-S . -B "%BINARY_DIR%" -G "%GEN%"
  if %SINGLE_CONFIG_GEN% EQU 1 set CFG_ARGS=%CFG_ARGS% -DCMAKE_BUILD_TYPE=%CONFIG%
  echo Configuring with: cmake %CFG_ARGS% %EXTRA_CMAKE_ARGS%
  if /I "%PRESET%"=="win-clangcl" set CFG_ARGS=%CFG_ARGS% -T ClangCL
  if /I "%PRESET%"=="win-clangcl-nogui" set CFG_ARGS=%CFG_ARGS% -T ClangCL
  echo %PRESET% | findstr /I /C:"-nogui" >nul && set CFG_ARGS=%CFG_ARGS% -DNO_GUI=ON
  cmake %CFG_ARGS% %EXTRA_CMAKE_ARGS%
  if %ERRORLEVEL% NEQ 0 set CONFIGURE_FAILED=1
)

if %CONFIGURE_FAILED% NEQ 0 (
  echo Initial configure failed.
  if exist vcpkg.json if /I NOT "%DISABLE_VCPKG%"=="1" (
    if %USE_VCPKG% EQU 0 (
      set TRY_VCPKG=
      if /I "%AUTO_VCPKG%"=="1" (
        set TRY_VCPKG=Y
      ) else if /I "%NONINTERACTIVE%"=="1" (
        set TRY_VCPKG=N
      ) else (
        set /p TRY_VCPKG=Dependencies may be missing (FreeImage/SDL2/SDL2_ttf). Attempt vcpkg fallback (local .vcpkg checkout)? [Y/n] 
      )
      if /I "%TRY_VCPKG%"=="n" (
        echo Skipping vcpkg fallback. Please install dependencies manually or set AUTO_VCPKG=1.
        echo Ubuntu:   sudo apt install libfreeimage-dev libsdl2-dev libsdl2-ttf-dev
        echo macOS:    brew install freeimage sdl2 sdl2_ttf
        echo Windows:  Install packages or set FREEIMAGE_DIR, SDL2_DIR, SDL2_TTF_DIR
        exit /b 1
      )
      if not defined VCPKG_ROOT (
        if exist .vcpkg\scripts\buildsystems\vcpkg.cmake (
          set VCPKG_ROOT=%CD%\.vcpkg
        ) else (
          where git >nul 2>&1 && (
            echo Bootstrapping local vcpkg under .vcpkg ...
            git clone --depth 1 https://github.com/microsoft/vcpkg.git .vcpkg
            if exist .vcpkg\bootstrap-vcpkg.bat (
              call .vcpkg\bootstrap-vcpkg.bat -disableMetrics
              set VCPKG_ROOT=%CD%\.vcpkg
            )
          )
        )
      )
      if not defined VCPKG_ROOT (
        echo Failed to prepare vcpkg. Aborting.
        exit /b 1
      )
      set USE_VCPKG=1
      set "EXTRA_CMAKE_ARGS=%EXTRA_CMAKE_ARGS% -DCMAKE_TOOLCHAIN_FILE=\"%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake\" -DVCPKG_FEATURE_FLAGS=manifests"
      echo Re-configuring with vcpkg toolchain ...
      if %SUPPORTS_PRESETS% EQU 1 (
        cmake --preset %PRESET% %EXTRA_CMAKE_ARGS%
      ) else (
        cmake %CFG_ARGS% %EXTRA_CMAKE_ARGS%
      )
      if %ERRORLEVEL% NEQ 0 (
        echo Configure with vcpkg failed. Aborting.
        exit /b 1
      )
    ) else (
      echo Configure failed even with vcpkg. Aborting.
      exit /b 1
    )
  ) else (
    echo Configure failed and vcpkg fallback is disabled or manifest missing.
    echo Hints: install system packages or pass -D*_DIR paths.
    exit /b 1
  )
)

if %CLEAN% EQU 1 (
  echo Cleaning in %BINARY_DIR% ^(config %CONFIG%^)...
  cmake --build %BINARY_DIR% --config %CONFIG% --target clean
)

if %CLEANONLY% EQU 1 goto :done

echo Building in %BINARY_DIR% ^(config %CONFIG%^)...
cmake --build %BINARY_DIR% --config %CONFIG%
if %ERRORLEVEL% NEQ 0 (
  echo Build failed!
  rem Offer vcpkg fallback on build-time failure if not already using it
  if exist vcpkg.json if /I NOT "%DISABLE_VCPKG%"=="1" if %USE_VCPKG% EQU 0 (
    set TRY_VCPKG=
    if /I "%AUTO_VCPKG%"=="1" (
      set TRY_VCPKG=Y
    ) else if /I "%NONINTERACTIVE%"=="1" (
      set TRY_VCPKG=N
    ) else (
      set /p TRY_VCPKG=Linker failure detected. Retry configure+build with vcpkg fallback? [Y/n] 
    )
    if /I not "%TRY_VCPKG%"=="n" (
      if not defined VCPKG_ROOT (
        if exist .vcpkg\scripts\buildsystems\vcpkg.cmake (
          set VCPKG_ROOT=%CD%\.vcpkg
        ) else (
          where git >nul 2>&1 && (
            echo Bootstrapping local vcpkg under .vcpkg ...
            git clone --depth 1 https://github.com/microsoft/vcpkg.git .vcpkg
            if exist .vcpkg\bootstrap-vcpkg.bat (
              call .vcpkg\bootstrap-vcpkg.bat -disableMetrics
              set VCPKG_ROOT=%CD%\.vcpkg
            )
          )
        )
      )
      if not defined VCPKG_ROOT (
        echo Failed to prepare vcpkg. Aborting.
        exit /b 1
      )
      set USE_VCPKG=1
      set "EXTRA_CMAKE_ARGS=%EXTRA_CMAKE_ARGS% -DCMAKE_TOOLCHAIN_FILE=\"%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake\" -DVCPKG_FEATURE_FLAGS=manifests"
      echo Re-configuring with vcpkg toolchain ...
      if %SUPPORTS_PRESETS% EQU 1 (
        if exist "%BINARY_DIR%" rmdir /s /q "%BINARY_DIR%"
        cmake --preset %PRESET% %EXTRA_CMAKE_ARGS%
      ) else (
        if exist "%BINARY_DIR%" rmdir /s /q "%BINARY_DIR%"
        cmake %CFG_ARGS% %EXTRA_CMAKE_ARGS%
      )
      if %ERRORLEVEL% NEQ 0 (
        echo Configure with vcpkg failed. Aborting.
        exit /b 1
      )
      echo Building with vcpkg toolchain ...
      cmake --build %BINARY_DIR% --config %CONFIG%
      if %ERRORLEVEL% NEQ 0 (
        echo Build failed even with vcpkg. Aborting.
        exit /b 1
      )
    ) else (
      exit /b 1
    )
  ) else (
    exit /b 1
  )
)

echo Build successful. Artifacts under out\build\%PRESET%\%CONFIG%\

goto :eof

:usage
echo Usage: build.bat [PRESET] [CONFIG] [CLEAN] [extra -D options]
echo Examples:
echo   build.bat win-clangcl Release -DTHREAD_DEBUG=ON -DUI_DEBUG=ON
echo   build.bat win-msvc Debug -DNO_GUI=ON
echo   build.bat win-msvc Release CLEAN
echo.
echo Available presets:
cmake --list-presets
exit /b 1

:done
endlocal
