@echo off
if /I "%BUILD_DEBUG%"=="1" @echo on
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
if errorlevel 1 (
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
) else if /I "%~2"=="RelWithDebInfo" (
  set CONFIG=RelWithDebInfo
  shift
) else if /I "%~2"=="MinSizeRel" (
  set CONFIG=MinSizeRel
  shift
)

REM Shift past preset
shift

:skip_preset_shift

:parse_args
if "%~1"=="" goto :args_done
if /I "%~1"=="CLEAN" (
	set CLEAN=1
	if /I "%BUILD_DEBUG%"=="1" echo [DEBUG] Parsed token: %~1  -> CLEAN=1
	shift
	goto :parse_args
)
if /I "%~1"=="CLEANONLY" (
	set CLEANONLY=1
	if /I "%BUILD_DEBUG%"=="1" echo [DEBUG] Parsed token: %~1  -> CLEANONLY=1
	shift
	goto :parse_args
)

set "ARG=%~1"
if /I "!ARG:~0,2!"=="-D" (
	rem -D style option
	if /I "%BUILD_DEBUG%"=="1" echo [DEBUG] Inspect -D token: ARG=!ARG!, NEXT="%~2"
	set "HAS_EQ="
	set "ARG_BEFORE_EQ="
	for /f "tokens=1* delims==" %%A in ("!ARG!") do (
		set "ARG_BEFORE_EQ=%%A"
		rem Fixed: Check if the part before = is different from original arg, indicating = was found
		if not "%%A"=="!ARG!" set HAS_EQ=1
	)
	if /I "%BUILD_DEBUG%"=="1" echo [DEBUG] HAS_EQ=!HAS_EQ!
	if defined HAS_EQ (
		rem Already -DVAR=VALUE (or -DVAR=) â€" wrap whole token in quotes to preserve spaces reliably
		set "EXTRA_CMAKE_ARGS=!EXTRA_CMAKE_ARGS! ^"!ARG!^""
		if /I "%BUILD_DEBUG%"=="1" echo [DEBUG] Added -D with equals (quoted): "!ARG!"
		shift
		goto :parse_args
	) else (
		rem Possibly split as: -DVAR  VALUE (consume next token if it is a value)
		if "%~2"=="" (
			set "EXTRA_CMAKE_ARGS=!EXTRA_CMAKE_ARGS! !ARG!"
			if /I "%BUILD_DEBUG%"=="1" echo [DEBUG] Added lone -D (no value): !ARG!
			shift
			goto :parse_args
		)
		set "NEXT_RAW=%~2"
		set "FIRSTCHAR=!NEXT_RAW:~0,1!"
		if "!FIRSTCHAR!"=="-" (
			set "EXTRA_CMAKE_ARGS=!EXTRA_CMAKE_ARGS! !ARG!"
			if /I "%BUILD_DEBUG%"=="1" echo [DEBUG] Next token looks like a switch; keeping !ARG! without value
			shift
			goto :parse_args
		)
		if "!FIRSTCHAR!"=="/" (
			set "EXTRA_CMAKE_ARGS=!EXTRA_CMAKE_ARGS! !ARG!"
			if /I "%BUILD_DEBUG%"=="1" echo [DEBUG] Next token looks like a switch; keeping !ARG! without value
			shift
			goto :parse_args
		)
		rem Join as a single quoted token: "-DVAR=VALUE" preserving spaces in VALUE
		set "EXTRA_CMAKE_ARGS=!EXTRA_CMAKE_ARGS! ^"!ARG!=%~2^""
		if /I "%BUILD_DEBUG%"=="1" echo [DEBUG] Joined split -D as single quoted token: "!ARG!=%~2"
		shift
		shift
		goto :parse_args
	)
) else (
	rem Positional argument (quote to preserve spaces)
	set "EXTRA_CMAKE_ARGS=!EXTRA_CMAKE_ARGS! \"%~1\""
	if /I "%BUILD_DEBUG%"=="1" echo [DEBUG] Added positional: "%~1"
	shift
	goto :parse_args
)

:args_done
if /I "%BUILD_DEBUG%"=="1" (
	echo.
	echo [DEBUG] Args parsed:
	echo   - PRESET=%PRESET%
	echo   - CONFIG=%CONFIG%
	echo   - CLEAN=%CLEAN%
	echo   - CLEANONLY=%CLEANONLY%
	echo   - EXTRA_CMAKE_ARGS=%EXTRA_CMAKE_ARGS%
	echo.
	rem pause
)


REM Detect single-config generators to pass CMAKE_BUILD_TYPE
REM Criteria: preset contains "-make" or is MinGW Makefiles presets
echo %PRESET% | findstr /I /C:"-make" >nul && set SINGLE_CONFIG_GEN=1
if /I "%PRESET%"=="win-mingw-gcc" set SINGLE_CONFIG_GEN=1
if /I "%PRESET%"=="win-mingw-gcc-nogui" set SINGLE_CONFIG_GEN=1

REM Prepare for optional vcpkg fallback later (do not enable by default)
if exist vcpkg.json (
  if /I "%DISABLE_VCPKG%"=="1" (
    rem vcpkg disabled explicitly
  ) else if /I "%USE_VCPKG%"=="1" (
    rem Explicit opt-in: USE_VCPKG=1 forces immediate use of vcpkg
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
      if exist "%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake" (
        set "EXTRA_CMAKE_ARGS=%EXTRA_CMAKE_ARGS% -DCMAKE_TOOLCHAIN_FILE=\"%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake\" -DVCPKG_FEATURE_FLAGS=manifests"
      ) else (
        echo USE_VCPKG=1 requested but VCPKG_ROOT toolchain not found; proceeding without vcpkg.
      )
    ) else (
      echo USE_VCPKG=1 requested but VCPKG_ROOT not available; proceeding without vcpkg.
    )
  )
)

REM Recompute binary dir after any preset auto-switching above
set BINARY_DIR=out\build\%PRESET%

if %SINGLE_CONFIG_GEN% EQU 1 (
  set "EXTRA_CMAKE_ARGS=%EXTRA_CMAKE_ARGS% -DCMAKE_BUILD_TYPE=%CONFIG%"
)

REM If CLEAN requested, remove build directory before configure (align with build.ps1)
if %CLEAN% EQU 1 (
  if exist "%BINARY_DIR%" (
    echo CLEAN requested: removing %BINARY_DIR% before configure ...
    rmdir /s /q "%BINARY_DIR%"
  )
)

REM Detect CMake version to decide if presets are supported
set CMAKE_MAJOR=0
set CMAKE_MINOR=0
set CMAKE_VER_TEMP=
for /f "delims=" %%v in ('cmake --version') do (
  if not defined CMAKE_VER_TEMP set "CMAKE_VER_TEMP=%%v"
)
for /f "tokens=3" %%v in ("%CMAKE_VER_TEMP%") do set CMAKE_VER_TEMP=%%v

if defined CMAKE_VER_TEMP (
  for /f "tokens=1,2 delims=." %%a in ("%CMAKE_VER_TEMP%") do (
    set CMAKE_MAJOR=%%a
    set CMAKE_MINOR=%%b
  )
)
set SUPPORTS_PRESETS=0
if not "%CMAKE_MAJOR%"=="0" if not "%CMAKE_MINOR%"=="" (
  if %CMAKE_MAJOR% GEQ 4 set SUPPORTS_PRESETS=1
  if %CMAKE_MAJOR% EQU 3 if %CMAKE_MINOR% GEQ 21 set SUPPORTS_PRESETS=1
)

rem Require modern CMake presets support
if %SUPPORTS_PRESETS% NEQ 1 (
  echo This project now requires CMake 3.21+ with presets. Please upgrade CMake.
  exit /b 1
)

if /I "%BUILD_DEBUG%"=="1" (
  echo.
  echo [DEBUG] CMake version detection:
  echo   - CMAKE_VER_TEMP=%CMAKE_VER_TEMP%
  echo   - CMAKE_MAJOR=%CMAKE_MAJOR%
  echo   - CMAKE_MINOR=%CMAKE_MINOR%
  echo   - SUPPORTS_PRESETS=%SUPPORTS_PRESETS%
  echo   - BINARY_DIR=%BINARY_DIR%
  echo.
  pause
)

set CONFIGURE_FAILED=0

echo Configuring with CMake preset %PRESET% ...
if not "%EXTRA_CMAKE_ARGS%"=="" echo Extra CMake args: %EXTRA_CMAKE_ARGS%
if /I "%BUILD_DEBUG%"=="1" (
  echo [DEBUG] About to run: cmake --preset %PRESET% %EXTRA_CMAKE_ARGS%
  pause
)
cmake --preset %PRESET% %EXTRA_CMAKE_ARGS%
if not exist "%BINARY_DIR%\CMakeCache.txt" (
  if /I "%BUILD_DEBUG%"=="1" echo [DEBUG] CMakeCache.txt not found. Retrying configure...
  echo Configure failed with presets, attempting a clean configure by removing %BINARY_DIR% ...
  if exist "%BINARY_DIR%" rmdir /s /q "%BINARY_DIR%"
  cmake --preset %PRESET% %EXTRA_CMAKE_ARGS%
)
if not exist "%BINARY_DIR%\CMakeCache.txt" (
  if /I "%BUILD_DEBUG%"=="1" echo [DEBUG] CMakeCache.txt still not found after retry. Setting CONFIGURE_FAILED=1
  set CONFIGURE_FAILED=1
)

:after_configure

if %CONFIGURE_FAILED% NEQ 0 (
  echo Initial configure failed.
  if /I "%BUILD_DEBUG%"=="1" (
    echo [DEBUG] CONFIGURE_FAILED is %CONFIGURE_FAILED%. Entering failure/vcpkg logic.
    pause
  )
  if exist vcpkg.json if /I NOT "%DISABLE_VCPKG%"=="1" (
    if %USE_VCPKG% EQU 0 (
      set TRY_VCPKG=
      if /I "%AUTO_VCPKG%"=="1" (
        set TRY_VCPKG=Y
      ) else if /I "%NONINTERACTIVE%"=="1" (
        set TRY_VCPKG=N
      ) else (
        set /p "TRY_VCPKG=Dependencies may be missing. Attempt vcpkg fallback? [Y/n] "
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
      if exist "%BINARY_DIR%" rmdir /s /q "%BINARY_DIR%"
      cmake --preset %PRESET% %EXTRA_CMAKE_ARGS%
      if not exist "%BINARY_DIR%\CMakeCache.txt" (
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
if errorlevel 1 (
  echo Build failed!
  rem Offer vcpkg fallback on build-time failure if not already using it
  if exist vcpkg.json if /I NOT "%DISABLE_VCPKG%"=="1" if %USE_VCPKG% EQU 0 (
    set TRY_VCPKG=
    if /I "%AUTO_VCPKG%"=="1" (
      set TRY_VCPKG=Y
    ) else if /I "%NONINTERACTIVE%"=="1" (
      set TRY_VCPKG=N
    ) else (
      set /p "TRY_VCPKG=Linker failure detected. Retry with vcpkg fallback? [Y/n] "
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
      if exist "%BINARY_DIR%" rmdir /s /q "%BINARY_DIR%"
      cmake --preset %PRESET% %EXTRA_CMAKE_ARGS%
      if not exist "%BINARY_DIR%\CMakeCache.txt" (
        echo Configure with vcpkg failed. Aborting.
        exit /b 1
      )
      echo Building with vcpkg toolchain ...
      cmake --build %BINARY_DIR% --config %CONFIG%
      if errorlevel 1 (
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
