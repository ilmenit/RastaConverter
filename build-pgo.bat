@echo off
setlocal ENABLEDELAYEDEXPANSION

REM RastaConverter LLVM-PGO automation for Intel icx on Windows (Ninja presets)
REM - Phase 1: configure+build instrumented, run scenarios to create .profraw files
REM - Merge profiles with llvm-profdata
REM - Phase 3: configure+build optimized with -fprofile-use

REM Change working dir to repo root (this script is expected to sit there)
cd /d "%~dp0"

echo === RastaConverter PGO (Intel icx, LLVM) ===
echo.
echo This script will:
echo   1) Configure and build an instrumented binary (icx -fprofile-generate)
echo   2) Run multiple training scenarios (500K-4M evaluations scaled by thread count) to produce .profraw files
echo   3) Merge profiles to merged.profdata using llvm-profdata
echo   4) Configure and build an optimized binary (icx -fprofile-use)
echo.

REM Check tools
cmake --version >nul 2>&1
if %errorlevel% neq 0 (
    echo [error] CMake not found in PATH. Please install CMake 3.21+ and add it to PATH.
    exit /b 1
)

REM Try Intel's llvm-profdata first, then fall back to system one
set LLVM_PROFDATA=
if exist "C:\Program Files (x86)\Intel\oneAPI\compiler\2025.2\bin\compiler\llvm-profdata.exe" (
    set LLVM_PROFDATA="C:\Program Files (x86)\Intel\oneAPI\compiler\2025.2\bin\compiler\llvm-profdata.exe"
) else if exist "C:\Program Files\Intel\oneAPI\compiler\2025.2\bin\compiler\llvm-profdata.exe" (
    set LLVM_PROFDATA="C:\Program Files\Intel\oneAPI\compiler\2025.2\bin\compiler\llvm-profdata.exe"
) else (
    where llvm-profdata >nul 2>&1
    if %errorlevel% neq 0 (
        echo [error] llvm-profdata not found in PATH.
        echo [hint] Open an Intel oneAPI Developer Command Prompt, or run oneAPI setvars.bat, then re-run this script.
        echo [hint] Example: ^"C:\Program Files ^(x86^)\Intel\oneAPI\setvars.bat^" intel64
        exit /b 1
    )
    set LLVM_PROFDATA=llvm-profdata
)
for /f "usebackq tokens=*" %%v in (`%LLVM_PROFDATA% --version 2^>^&1`) do set LLVMPROFDATA_VERSION=%%v
echo [info] llvm-profdata: %LLVMPROFDATA_VERSION%

REM Check if Ninja is available, otherwise use Visual Studio
where ninja >nul 2>&1
if %errorlevel% equ 0 (
    set PRESET_GEN=ninja-pgo-icx-gen
    set PRESET_USE=ninja-pgo-icx-use
    echo [info] Using Ninja generator (ninja found in PATH)
) else (
    set PRESET_GEN=x64-pgo-icx-gen
    set PRESET_USE=x64-pgo-icx-use
    echo [info] Using Visual Studio generator (ninja not found in PATH)
)

echo [info] PGO Profile Generation Preset: %PRESET_GEN%
echo [info] PGO Profile Usage Preset: %PRESET_USE%
echo [info] Profile Directory: %PROFDIR%
echo [info] Test Image: %TESTIMG%
set PROFDIR=%CD%\pgo\icx
set RUNDIR_PREFERRED=%CD%\build\%PRESET_GEN%\Release
set RUNDIR_FALLBACK=%CD%\build\%PRESET_GEN%
set RUNEXE=RastaConverter.exe
set TESTIMG=%~1
if "%TESTIMG%"=="" set TESTIMG=test.jpg

if not exist "%PROFDIR%" mkdir "%PROFDIR%"

echo [step] Configure (instrumented) preset=%PRESET_GEN%
cmake --preset %PRESET_GEN% -DCOPY_ALL_RUNTIME_DLLS=ON
if %errorlevel% neq 0 (
    echo [error] Configure [gen] failed.
    exit /b 1
)

echo [step] Build (instrumented)
echo [info] Compiler and build configuration:
cmake -LA -N build\%PRESET_GEN% 2>nul | findstr /i "CMAKE_C_COMPILER CMAKE_CXX_COMPILER CMAKE_BUILD_TYPE CMAKE_C_FLAGS CMAKE_CXX_FLAGS"
cmake --build --preset %PRESET_GEN%
if %errorlevel% neq 0 (
    echo [error] Build [gen] failed.
    exit /b 1
)

set RUNDIR=%RUNDIR_PREFERRED%
if not exist "%RUNDIR%\%RUNEXE%" (
    if exist "%RUNDIR_FALLBACK%\%RUNEXE%" (
        set RUNDIR=%RUNDIR_FALLBACK%
    ) else (
        if exist "%RUNDIR_PREFERRED%" (
            echo [warn] Executable not found in preferred run dir, trying fallback.
        )
        echo [error] Could not locate %RUNEXE% in:
        echo   - %RUNDIR_PREFERRED%
        echo   - %RUNDIR_FALLBACK%
        exit /b 1
    )
)
echo [info] Run directory: %RUNDIR%

REM Ensure test image exists next to the executable
if not exist "%RUNDIR%\%TESTIMG%" (
    if exist "%CD%\%TESTIMG%" (
        echo [info] Copying %TESTIMG% to run directory...
        copy /y "%CD%\%TESTIMG%" "%RUNDIR%\%TESTIMG%" >nul
    ) else (
        echo [error] %TESTIMG% not found in repo root or run dir. Provide an input image or pass a path as the first argument.
        echo [hint] Example: build-pgo.bat examples\test.jpg
        exit /b 1
    )
)

echo [step] Run training scenarios (writing .profraw into %PROFDIR%)
echo [info] Training scenarios:
echo [info]   01 base: 500K evaluations (1 thread)
echo [info]   02 t4:   2M evaluations (4 threads)
echo [info]   03 t8:   4M evaluations (8 threads)
echo [info]   04 dual: 500K evaluations (1 thread, dual mode)
echo [info]   05 dual t4: 2M evaluations (4 threads, dual mode)
echo [info]   06 dual t8: 4M evaluations (8 threads, dual mode)
pushd "%RUNDIR%" >nul

set "LLVM_PROFILE_FILE=%PROFDIR%\rasta-01-base.profraw"
echo [run] 01 base: %RUNEXE% %TESTIMG% /max_evals=500000
"%RUNEXE%" "%TESTIMG%" /max_evals=500000
if %errorlevel% neq 0 goto run_fail
if not exist "%LLVM_PROFILE_FILE%" ( echo [error] Expected profile not written: %LLVM_PROFILE_FILE% & goto run_fail )

set "LLVM_PROFILE_FILE=%PROFDIR%\rasta-02-t4.profraw"
echo [run] 02 t4: %RUNEXE% %TESTIMG% /threads=4 /max_evals=2000000
"%RUNEXE%" "%TESTIMG%" /threads=4 /max_evals=2000000
if %errorlevel% neq 0 goto run_fail
if not exist "%LLVM_PROFILE_FILE%" ( echo [error] Expected profile not written: %LLVM_PROFILE_FILE% & goto run_fail )

set "LLVM_PROFILE_FILE=%PROFDIR%\rasta-03-t8.profraw"
echo [run] 03 t8: %RUNEXE% %TESTIMG% /threads=8 /max_evals=4000000
"%RUNEXE%" "%TESTIMG%" /threads=8 /max_evals=4000000
if %errorlevel% neq 0 goto run_fail
if not exist "%LLVM_PROFILE_FILE%" ( echo [error] Expected profile not written: %LLVM_PROFILE_FILE% & goto run_fail )

set "LLVM_PROFILE_FILE=%PROFDIR%\rasta-04-dual.profraw"
echo [run] 04 dual: %RUNEXE% %TESTIMG% /dual /max_evals=500000
"%RUNEXE%" "%TESTIMG%" /dual /max_evals=500000
if %errorlevel% neq 0 goto run_fail
if not exist "%LLVM_PROFILE_FILE%" ( echo [error] Expected profile not written: %LLVM_PROFILE_FILE% & goto run_fail )

set "LLVM_PROFILE_FILE=%PROFDIR%\rasta-05-dual-t4.profraw"
echo [run] 05 dual t4: %RUNEXE% %TESTIMG% /dual /threads=4 /max_evals=2000000
"%RUNEXE%" "%TESTIMG%" /dual /threads=4 /max_evals=2000000
if %errorlevel% neq 0 goto run_fail
if not exist "%LLVM_PROFILE_FILE%" ( echo [error] Expected profile not written: %LLVM_PROFILE_FILE% & goto run_fail )

set "LLVM_PROFILE_FILE=%PROFDIR%\rasta-06-dual-t8.profraw"
echo [run] 06 dual t8: %RUNEXE% %TESTIMG% /dual /threads=8 /max_evals=4000000
"%RUNEXE%" "%TESTIMG%" /dual /threads=8 /max_evals=4000000
if %errorlevel% neq 0 goto run_fail
if not exist "%LLVM_PROFILE_FILE%" ( echo [error] Expected profile not written: %LLVM_PROFILE_FILE% & goto run_fail )

popd >nul

set FILELIST=
for %%F in ("%PROFDIR%\*.profraw") do (
    set FILELIST=!FILELIST! "%%~fF"
)
if "!FILELIST!"=="" (
    echo [error] No .profraw files found in %PROFDIR%.
    exit /b 1
)

echo [step] Merge profiles into merged.profdata
echo   Input: %PROFDIR%\*.profraw
%LLVM_PROFDATA% merge -output="%PROFDIR%\merged.profdata" !FILELIST!
if %errorlevel% neq 0 (
    echo [error] llvm-profdata merge failed.
    exit /b 1
)

echo [step] Configure (use) preset=%PRESET_USE%
cmake --preset %PRESET_USE% -DCOPY_ALL_RUNTIME_DLLS=ON
if %errorlevel% neq 0 (
    echo [error] Configure [use] failed.
    exit /b 1
)

echo [step] Build (optimized)
cmake --build --preset %PRESET_USE%
if %errorlevel% neq 0 (
    echo [error] Build [use] failed.
    exit /b 1
)

echo [success] PGO build complete.
echo [hint] Optimized artifacts may be under:
echo   - build\%PRESET_USE%\Release\
echo   - build\%PRESET_USE%\
echo [hint] Profiles: %PROFDIR%\merged.profdata (raw files kept)
exit /b 0

:run_fail
popd >nul
echo [error] Scenario run failed with exit code %errorlevel%.
exit /b 1


