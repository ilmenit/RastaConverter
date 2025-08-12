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

where cmake >nul 2>&1
if %ERRORLEVEL% NEQ 0 (
  echo CMake is required but not installed. Aborting.
  exit /b 1
)

if "%~1"=="" goto :usage
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

set BINARY_DIR=out\build\%PRESET%

echo Configuring with CMake preset %PRESET% ...
if not "%EXTRA_CMAKE_ARGS%"=="" (
  echo Extra CMake args:%EXTRA_CMAKE_ARGS%
)
cmake --preset %PRESET% %EXTRA_CMAKE_ARGS%
if %ERRORLEVEL% NEQ 0 (
  echo Configure failed, attempting a clean configure by removing %BINARY_DIR% ...
  if exist "%BINARY_DIR%" rmdir /s /q "%BINARY_DIR%"
  cmake --preset %PRESET% %EXTRA_CMAKE_ARGS%
)

if %ERRORLEVEL% NEQ 0 (
  echo Configure failed. Aborting.
  exit /b 1
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
  exit /b 1
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
